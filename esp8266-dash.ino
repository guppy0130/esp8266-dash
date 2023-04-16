// clang-format off
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <ESP8266WiFi.h>
#include <TZ.h>
#include <sntp.h>
#include <locale.h>
#include <string.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <StreamUtils.h>

// refer to arduino_secrets.h.example
#include "arduino_secrets.h"
#include "GxEPD2_display_selection_new_style.h"
// clang-format on

// timezone of device. Since there's no GPS module, this has to be set here.
#define MYTZ TZ_America_Chicago
// display margin, so we're not up against the edges of the display.
// TODO: use correctly
const uint DISPLAY_MARGIN = 4;
// how often to fetch new data + update display in minutes.
const uint REFRESH_RATE_MINS = 30;

void setup() {
    // init serial for debug
    Serial.begin(115200);
    Serial.println();
    // init e-paper display
    display.init(115200);
    // init wifi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to wifi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.print("Connected, IP address: ");
    Serial.println(WiFi.localIP());
    // ntp
    configTime(MYTZ, "pool.ntp.org");
    // first time fetch to tickle NTP. This call does not affect the screen
    // because we'll immediately clearScreen() right after. This value is
    // usually epoch because NTP hasn't been tickled yet.
    render_time();
    delay(1000);
    clearScreen();
}

/**
 * Generic e-paper display resetter.
 *
 * Sets rotation, sets full screen, fills the screen with white. Sets the font
 * and the font color.
 */
void clearScreen() {
    display.setRotation(2);
    display.setFullWindow();
    display.firstPage();
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
}

/**
 * Fetches current time from NTP and renders it to display and serial.
 *
 * Fetches the current time via ESP8266 library functions and displays it at the
 * bottom of the display. Also prints the content to serial.
 */
void render_time() {
    char buffer[80];
    time_t tnow = time(nullptr);
    struct tm *timeinfo;

    timeinfo = localtime(&tnow);
    strftime(buffer, 80, "Last updated %x %X", timeinfo);
    Serial.println(buffer);
    display.setCursor(DISPLAY_MARGIN, display.height() - DISPLAY_MARGIN);
    display.print(buffer);
}

/**
 * Fetch and render the weather to display + serial.
 *
 * Fetches weather data from the included server. Expects the server to provide
 * already-formatted data, and directly prints this data to the display.
 */
void render_weather() {
    // TODO: switch to using HTTPS if you don't trust your server. Note that
    // you will pay CPU cycles.
    std::unique_ptr<WiFiClient> client(new WiFiClient);
    HTTPClient http;
    char url[128];
    snprintf(url, 128, "%s/forecast/%.3f,%.3f", SERVER_URL, lat, lon);
    if (http.begin(*client, url)) {
        int httpCode = http.GET();
        Serial.printf("Received %d from %s\n", httpCode, url);
        if (httpCode != HTTP_CODE_OK) {
            return;
        }
        String s = http.getString();
        display.setCursor(0, DISPLAY_MARGIN + 9);
        display.println(s);
        Serial.println(s);
    }
}

void loop() {
    clearScreen();
    render_time();
    render_weather();
    display.display();
    display.hibernate();
    delay(1000 * 60 * REFRESH_RATE_MINS);
};
