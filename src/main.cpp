#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <driver/uart.h>

#include <Adafruit_SHT31.h>
#include <PsychicHttp.h>
#include <s8_uart.h>

#include "wifiSecrets.h"

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

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(true);

    while (!Serial)
        delay(1);

    delay(5000);

    Serial.println("SHT31 test");
    Wire.begin(1, 2);
    if (!sht31.begin(0x44))
    {
        Serial.println("Couldn't find SHT31");
        while (1)
            delay(1);
    }

    Serial.print("Heater Enabled State: ");
    if (sht31.isHeaterEnabled())
        Serial.println("ENABLED");
    else
        Serial.println("DISABLED");

    log_i("temp %.1f", sht31.readTemperature());

    Serial1.setPins(21, 20);
    Serial1.begin(S8_BAUDRATE);
    sensor_S8 = new S8_UART(Serial1);

    if (!sensor_S8)
    {
        log_i("FATAL ERROR! Could not initialize CO2 sensor.");
        while (1)
            delay(100);
    }

    log_i("CO2 level, %i", sensor_S8->get_co2());

    /* try to set a static IP */
    if (!WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS_SERVER))
        Serial.println("Setting static IP failed");

    // connect to wifi
    WiFi.mode(WIFI_MODE_AP);
    WiFi.setAutoReconnect(true);
    WiFi.begin(SSID, PSK);
    WiFi.setSleep(false);
    while (!WiFi.isConnected())
        delay(10);

    Serial.printf("Connected to %s\n", SSID);
    Serial.printf("IP %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Sensor websocket started at http://%s%s\n", WiFi.localIP().toString().c_str(), SENSORS_WS_URL);

    // setup the webserver

    server.config.max_uri_handlers = 8;
    server.config.max_open_sockets = 8;
    server.listen(80);

    websocketHandler.onOpen([](PsychicWebSocketClient *client)
    {
        Serial.printf("[socket] connection #%u connected from %s\n", client->socket(), client->remoteIP().toString());
        client->sendMessage("Hello!"); 
    });

    websocketHandler.onClose([](PsychicWebSocketClient *client)
    { 
        Serial.printf("[socket] connection #%u closed from %s\n", client->socket(), client->remoteIP().toString()); 
    });

    server.on(SENSORS_WS_URL, &websocketHandler);

    server.on("/ip", [](PsychicRequest *request)
    {
        String output = "Your IP is: " + request->client()->remoteIP().toString();
        return request->reply(output.c_str()); 
    });

    server.on("/hello", HTTP_GET, [](PsychicRequest *request)
    {
        String hello = "Hello world!";
        return request->reply(200, "text/html", hello.c_str()); 
    });

    server.onNotFound([](PsychicRequest *request)
    { 
        return request->reply(404, "text/html", "No page here. Use websocket to connect to /ws."); 
    });
}

bool enableHeater = false;
uint8_t loopCnt = 0;

void loop()
{
    static int32_t lastCo2 = 0;
    static float lastTemp = -200;
    static int32_t lastHumidity = 0;

    int32_t co2Level = sensor_S8->get_co2();
    float temp = sht31.readTemperature();
    int32_t humidity = sht31.readHumidity(); // save the int part to reduce noise

    temp = static_cast<float>(static_cast<int>(temp * 10.)) / 10.; // round off to 1 decimal place to reduce noise

    char responseBuffer[16];

    if (co2Level && co2Level != lastCo2)
    {
        snprintf(responseBuffer, sizeof(responseBuffer), "C:%i", co2Level);
        websocketHandler.sendAll(responseBuffer);
        Serial.printf("Co2 level updated to %i\n", co2Level);
        lastCo2 = co2Level;
    }

    if (!isnan(temp) && temp != lastTemp)
    {
        snprintf(responseBuffer, sizeof(responseBuffer), "T:%.1f", temp);
        websocketHandler.sendAll(responseBuffer);
        Serial.printf("temp updated to %.1f\n", temp);
        lastTemp = temp;
    }

    if (!isnan(humidity) && humidity != lastHumidity)
    {
        snprintf(responseBuffer, sizeof(responseBuffer), "H:%i", humidity);
        websocketHandler.sendAll(responseBuffer);
        Serial.printf("humidity updated to %i\n", humidity);
        lastHumidity = humidity;
    }

    delay(200);
}
