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

#define NTP_POOL "nl.pool.ntp.org"               /* change 'nl' to your countries ISO code for better latency */
#define TIMEZONE "CET-1CEST,M3.5.0/2,M10.5.0/3"; /* Central European Time - see https://sites.google.com/a/usapiens.com/opnode/time-zones */

static Adafruit_SHT31 sht31 = Adafruit_SHT31();
static S8_UART *sensor_S8;

static PsychicHttpServer server;
static PsychicWebSocketHandler websocketHandler;

static storageStruct lastResults{-200 /* temp */,
                                 0 /* co2 */,
                                 0 /* humidity */};
static storageStruct history[90]; // this should be a ringbuffer in psram to hold significant data

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(true);
/*
    while (!Serial)
        delay(1);

    delay(5000);
*/
    SPI.setHwCs(true);
    SPI.begin(SCK, MISO, MOSI);
    if (!SD.begin(SS))
        log_e("No SDcard");

    log_i("SDcard mounted");

    Wire.begin(SHT31_SDA, SHT31_SCL);
    if (!sht31.begin(SHT31_DEFAULT_ADDR))
    {
        log_e("FATAL ERROR! Couldn't find SHT31 at address %u", SHT31_DEFAULT_ADDR);
        pinMode(BUILTIN_LED, OUTPUT);
        while (1)
        {
            digitalWrite(BUILTIN_LED, HIGH);
            delay(100);
            digitalWrite(BUILTIN_LED, LOW);
            delay(100);
        }
    }

    log_i("temp %.1f", sht31.readTemperature());

    Serial1.setPins(SENSEAIR_RXD, SENSEAIR_TXD);
    Serial1.begin(S8_BAUDRATE);
    sensor_S8 = new S8_UART(Serial1);

    if (!sensor_S8)
    {
        log_e("FATAL ERROR! Could not initialize CO2 sensor.");
        pinMode(BUILTIN_LED, OUTPUT);
        while (1)
        {
            digitalWrite(BUILTIN_LED, HIGH);
            delay(100);
            digitalWrite(BUILTIN_LED, LOW);
            delay(100);
        }
    }

    log_i("CO2 level, %i", sensor_S8->get_co2());

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
    log_i("Sensor websocket started at http://%s%s", WiFi.localIP().toString().c_str(), SENSORS_WS_URL);

    // setup the webserver

    server.config.max_uri_handlers = 8;
    server.config.max_open_sockets = 8;
    server.listen(80);

    websocketHandler.onOpen([](PsychicWebSocketClient *client)
                            {
        log_v("[socket] connection #%u connected from %s", client->socket(), client->remoteIP().toString());
        char buff[16];
        snprintf(buff, sizeof(buff), "H:%i", lastResults.humidity);
        client->sendMessage(buff);
        snprintf(buff, sizeof(buff), "T:%.1f", lastResults.temp);
        client->sendMessage(buff);
        snprintf(buff, sizeof(buff), "C:%i", lastResults.co2);
        client->sendMessage(buff); });

    websocketHandler.onClose([](PsychicWebSocketClient *client)
                             { Serial.printf("[socket] connection #%u closed from %s\n", client->socket(), client->remoteIP().toString()); });

    server.on(SENSORS_WS_URL, &websocketHandler);

    server.on("/ip", [](PsychicRequest *request)
              {
        String output = "Your IP is: " + request->client()->remoteIP().toString();
        return request->reply(output.c_str()); });

    server.on("/hello", HTTP_GET, [](PsychicRequest *request)
              {
        String hello = "Hello world!";
        return request->reply(200, "text/html", hello.c_str()); });

    server.onNotFound([](PsychicRequest *request)
                      { return request->reply(404, "text/html", "No page here. Use websocket to connect to /ws."); });
}

void loop()
{
    int32_t co2Level = sensor_S8->get_co2();
    float temp = sht31.readTemperature();
    int32_t humidity = sht31.readHumidity(); // only use the integer part to reduce noise

    temp = static_cast<float>(static_cast<int>(temp * 10.)) / 10.; // round off to 1 decimal place to reduce noise

    static char responseBuffer[16];

    if (co2Level && co2Level != lastResults.co2)
    {
        snprintf(responseBuffer, sizeof(responseBuffer), "C:%i", co2Level);
        websocketHandler.sendAll(responseBuffer);
        lastResults.co2 = co2Level;
    }

    if (!isnan(temp) && temp != lastResults.temp)
    {
        snprintf(responseBuffer, sizeof(responseBuffer), "T:%.1f", temp);
        websocketHandler.sendAll(responseBuffer);
        lastResults.temp = temp;
    }

    if (!isnan(humidity) && humidity != lastResults.humidity)
    {
        snprintf(responseBuffer, sizeof(responseBuffer), "H:%i", humidity);
        websocketHandler.sendAll(responseBuffer);
        lastResults.humidity = humidity;
    }

    delay(200);
}
