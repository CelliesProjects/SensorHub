#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <driver/uart.h>

#include <Adafruit_SHT31.h>
#include <PsychicHttp.h>
#include <s8_uart.h>

#include "wifiSecrets.h"
#include "storageStruct.h"

IPAddress STATIC_IP(192, 168, 0, 20);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress GATEWAY(192, 168, 0, 1);
IPAddress DNS_SERVER(192, 168, 0, 10);

#define SENSORS_WS_URL "/sensors"
#define SAVE_TIME_MIN (1) /* data save interval in minutes */

#ifndef NTP_POOL
#define NTP_POOL "nl.pool.ntp.org" /* change 'nl' to your countries ISO code for better latency */
#endif
#ifndef TIMEZONE
#define TIMEZONE "CET-1CEST,M3.5.0/2,M10.5.0/3" /* Central European Time - see https://sites.google.com/a/usapiens.com/opnode/time-zones */
#endif

static Adafruit_SHT31 sht31 = Adafruit_SHT31();
static S8_UART *sensor_S8;

static PsychicHttpServer server;
static PsychicWebSocketHandler websocketHandler;

static struct storageStruct lastResults = {NAN, 0, 0};

static std::list<struct storageStruct> history;

const auto UPDATE_INTERVAL_MS = 1000;

/* send only 1 update per UPDATE_INTERVAL_MS for every value */
static auto lastTempResponseMS = 0;
static auto lastHumidityResponseMS = 0;
static auto lastCo2ResponseMS = 0;

void fatalError(const char *str)
{
    log_e("FATAL ERROR %s - SYTEM HALTED", str);

    while (1)
    {
        digitalWrite(BUILTIN_LED, HIGH);
        delay(100);
        digitalWrite(BUILTIN_LED, LOW);
        delay(100);
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    /*
        while (!Serial)
            delay(1);

        delay(2000);
    */
    Serial.println("SensorHub");

    pinMode(BUILTIN_LED, OUTPUT);

    SPI.setHwCs(true);
    SPI.begin(SCK, MISO, MOSI);
    bool result = SD.begin(SS);

    log_i("SDcard %s mounted", result ? "is" : "not");

    Wire.begin(SHT31_SDA, SHT31_SCL);
    if (!sht31.begin(SHT31_DEFAULT_ADDR))
        fatalError("No SHT31 sensor");

    Serial1.begin(S8_BAUDRATE, SERIAL_8N1, SENSEAIR_RXD, SENSEAIR_TXD);
    sensor_S8 = new S8_UART(Serial1);

    if (!sensor_S8)
        fatalError("Could not initialize CO2 sensor");

    /* try to set a static IP */
    if (!WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS_SERVER))
        log_e("Setting static IP failed");

    // connect to wifi
    WiFi.mode(WIFI_MODE_AP);
    WiFi.setAutoReconnect(true);
    WiFi.begin(SSID, PSK);
    WiFi.setSleep(false);
    while (!WiFi.isConnected())
        delay(10);

    log_i("Connected to %s", SSID);
    log_i("IP %s", WiFi.localIP().toString().c_str());

    /* sync the clock with ntp */
    Serial.println("syncing NTP");
    configTzTime(TIMEZONE, NTP_POOL);

    tm now{};

    while (!getLocalTime(&now, 0))
        delay(10);

    Serial.println("NTP synced");

    log_i("Sensor websocket started at http://%s%s", WiFi.localIP().toString().c_str(), SENSORS_WS_URL);

    // setup the webserver

    server.config.max_uri_handlers = 8;
    server.config.max_open_sockets = 8;
    server.listen(80);

    websocketHandler.onOpen(
        [](PsychicWebSocketClient *client)
        {
            Serial.printf("[socket] connection #%u connected from %s\n", client->socket(), client->remoteIP().toString());
            if (lastResults.temp == NAN)
                return;
            char buff[16];
            snprintf(buff, sizeof(buff), "H:%i", lastResults.humidity);
            client->sendMessage(buff);
            lastHumidityResponseMS = millis();

            snprintf(buff, sizeof(buff), "T:%.1f", lastResults.temp);
            client->sendMessage(buff);
            lastTempResponseMS = millis();

            snprintf(buff, sizeof(buff), "C:%i", lastResults.co2);
            client->sendMessage(buff);
            lastCo2ResponseMS = millis();
        });

    websocketHandler.onFrame(
        [](PsychicWebSocketRequest *request, httpd_ws_frame *frame)
        {
            log_v("[socket] #%d sent: %s\n", request->client()->socket(), (char *)frame->payload);
            const char *emptyList = "G:\n";

            if (!strcmp((char *)frame->payload, emptyList)) // we have a request for history
            {
                if (history.empty())
                    return request->reply(emptyList);

                String wsResponse = emptyList;
                for (auto const &item : history)
                {
                    wsResponse.concat("T:");
                    wsResponse.concat(item.temp);
                    wsResponse.concat('\t');

                    wsResponse.concat("C:");
                    wsResponse.concat(item.co2);
                    wsResponse.concat('\t');

                    wsResponse.concat("H:");
                    wsResponse.concat(item.humidity);
                    wsResponse.concat('\n');
                }
                return request->reply(wsResponse.c_str());
            }
            log_w("unknown command %s\n", (char *)frame->payload);
            return request->reply("unknown command");
        });

    websocketHandler.onClose(
        [](PsychicWebSocketClient *client)
        {
            Serial.printf("[socket] connection #%u closed from %s\n", client->socket(), client->remoteIP().toString());
        });

    server.on(SENSORS_WS_URL, HTTP_GET, &websocketHandler);

    server.onNotFound(
        [](PsychicRequest *request)
        {
            return request->reply(404, "text/html", "No pages on this site. Use websocket to connect to /ws.");
        });

    // example callback everytime a connection is opened
    server.onOpen(
        [](PsychicClient *client)
        {
            Serial.printf("[http] connection #%u connected from %s\n", client->socket(), client->remoteIP().toString());
        });

    // example callback everytime a connection is closed
    server.onClose(
        [](PsychicClient *client)
        {
            Serial.printf("[http] connection #%u closed from %s\n", client->socket(), client->remoteIP().toString());
        });
}

static struct storageStruct average = {0, 0, 0};
static uint32_t numberOfSamples{0};

void saveAverage()
{
    average.co2 /= numberOfSamples;
    average.temp /= numberOfSamples;
    average.humidity /= numberOfSamples;
    log_v("saving the average of %i samples: temp %.1f\tco2\t%ippm\thumidity %i%%\n", numberOfSamples, average.temp, average.co2, average.humidity);

    const auto MAX_HISTORY_ITEMS = 180;
    static auto numberOfItems = 0;

    if (numberOfItems == MAX_HISTORY_ITEMS)
    {
        history.pop_back();
        history.push_front(average);
    }
    else
    {
        history.push_front(average);
        numberOfItems++;
    }
    const auto clist = websocketHandler.getClientList();
    if (!clist.empty())
    {
        static char responseBuffer[32];
        snprintf(responseBuffer, sizeof(responseBuffer), "A:\nT:%.1f\tC:%i\tH:%i\n", average.temp, average.co2, average.humidity);
        websocketHandler.sendAll(responseBuffer);
    }

    // we are done, reset averages
    average = {0, 0, 0};
    numberOfSamples = 0;
}

constexpr const auto TICK_RATE_HZ = 50;

constexpr const TickType_t ticksToWait = pdTICKS_TO_MS(1000 / TICK_RATE_HZ);
static TickType_t xLastWakeTime = xTaskGetTickCount();

void loop()
{
    vTaskDelayUntil(&xLastWakeTime, ticksToWait);

    static time_t lastSecond = time(NULL);
    if (time(NULL) != lastSecond)
    {
        average.temp += lastResults.temp;
        average.co2 += lastResults.co2;
        average.humidity += lastResults.humidity;
        numberOfSamples++;
        lastSecond = time(NULL);

        const auto clist = websocketHandler.getClientList();
        if (!(lastSecond % 3) && !clist.empty())
            websocketHandler.sendAll("P:"); /* send a ping to indicate we are still connected */
    }

    static tm now;
    getLocalTime(&now);
    if ((59 == now.tm_sec) && !(now.tm_min % SAVE_TIME_MIN) && (numberOfSamples > 2))
        saveAverage();

    const int32_t co2Level = sensor_S8->get_co2();
    float temp = sht31.readTemperature();          // readTemperature() delays for 20ms
    const int32_t humidity = sht31.readHumidity(); // only use the integer part to reduce noise

    temp = static_cast<float>(static_cast<int>(temp * 10.)) / 10.; // round off to 1 decimal place to reduce noise

    static char responseBuffer[16];

    // co2 level
    if (millis() - lastCo2ResponseMS > UPDATE_INTERVAL_MS && co2Level != lastResults.co2)
    {
        const auto clist = websocketHandler.getClientList();
        if (!clist.empty())
        {
            snprintf(responseBuffer, sizeof(responseBuffer), "C:%i", co2Level);
            digitalWrite(BUILTIN_LED, HIGH);
            websocketHandler.sendAll(responseBuffer);
            digitalWrite(BUILTIN_LED, LOW);
        }
        lastResults.co2 = co2Level;
        lastCo2ResponseMS = millis();
    }

    // temperature
    if (millis() - lastTempResponseMS > UPDATE_INTERVAL_MS && !isnan(temp) && temp != lastResults.temp)
    {
        const auto clist = websocketHandler.getClientList();
        if (!clist.empty())
        {
            snprintf(responseBuffer, sizeof(responseBuffer), "T:%.1f", temp);
            digitalWrite(BUILTIN_LED, HIGH);
            websocketHandler.sendAll(responseBuffer);
            digitalWrite(BUILTIN_LED, LOW);
        }
        lastResults.temp = temp;
        lastTempResponseMS = millis();
    }

    // humidity
    if (millis() - lastHumidityResponseMS > UPDATE_INTERVAL_MS && !isnan(humidity) && humidity != lastResults.humidity)
    {
        const auto clist = websocketHandler.getClientList();
        if (!clist.empty())
        {
            snprintf(responseBuffer, sizeof(responseBuffer), "H:%i", humidity);
            digitalWrite(BUILTIN_LED, HIGH);
            websocketHandler.sendAll(responseBuffer);
            digitalWrite(BUILTIN_LED, LOW);
        }
        lastResults.humidity = humidity;
        lastHumidityResponseMS = millis();
    }
}
