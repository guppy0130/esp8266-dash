// clang-format off
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <ESP8266WiFi.h>
#include <TZ.h>
#include <sntp.h>
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
    WiFi.hostname("esp8266-dash-client");
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

uint16_t read16(std::unique_ptr<WiFiClient> &client) {
    // BMP data is stored little-endian, same as Arduino.
    uint16_t result;
    ((uint8_t *)&result)[0] = client->read();  // LSB
    ((uint8_t *)&result)[1] = client->read();  // MSB
    // Serial.printf("read16: %x\n", result);
    return result;
}

int32_t read32(std::unique_ptr<WiFiClient> &client) {
    // BMP data is stored little-endian, same as Arduino.
    int32_t result;
    ((uint8_t *)&result)[0] = client->read();  // LSB
    ((uint8_t *)&result)[1] = client->read();
    ((uint8_t *)&result)[2] = client->read();
    ((uint8_t *)&result)[3] = client->read();  // MSB
    // Serial.printf("read32: %x\n", result);
    return result;
}

uint32_t skip(std::unique_ptr<WiFiClient> &client, int32_t bytes) {
    int32_t remain = bytes;
    uint32_t start = millis();
    while ((client->connected() || client->available()) && (remain > 0)) {
        if (client->available()) {
            int16_t v = client->read();
            (void)v;
            remain--;
        } else
            delay(1);
        if (millis() - start > 2000) break;  // don't hang forever
    }
    return bytes - remain;
}

uint32_t read8n(std::unique_ptr<WiFiClient> &client, uint8_t *buffer, int32_t bytes) {
    int32_t remain = bytes;
    uint32_t start = millis();
    while ((client->connected() || client->available()) && (remain > 0)) {
        if (client->available()) {
            int16_t v = client->read();
            *buffer++ = uint8_t(v);
            remain--;
        } else
            delay(1);
        if (millis() - start > 2000) break;  // don't hang forever
    }
    return bytes - remain;
}

static const uint16_t input_buffer_pixels = 800;       // may affect performance
static const uint16_t max_row_width = 400;             // for up to 4.2" display 400x300
static const uint16_t max_palette_pixels = 256;        // for depth <= 8
uint8_t input_buffer[3 * input_buffer_pixels];         // up to depth 24
uint8_t output_row_mono_buffer[max_row_width / 8];     // buffer for at least one row of b/w bits
uint8_t output_row_color_buffer[max_row_width / 8];    // buffer for at least one row of color bits
uint8_t mono_palette_buffer[max_palette_pixels / 8];   // palette buffer for depth <= 8 b/w
uint8_t color_palette_buffer[max_palette_pixels / 8];  // palette buffer for depth <= 8 c/w

void draw_bitmap_http(const char *host, uint16_t port, char *path, int16_t x, int16_t y, bool with_color) {
    std::unique_ptr<WiFiClient> client(new WiFiClient);
    HTTPClient http;
    if (!client->connect(host, port)) {
        Serial.printf("Unable to connect to %s:%d", host, port);
        return;
    }

    bool connection_ok = false;
    bool valid = false;  // valid format to be handled
    bool flip = true;    // bitmap is stored bottom-to-top

    client->print(String("GET ") + path + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
    // read headers
    while (client->connected()) {
        String line = client->readStringUntil('\n');
        Serial.println(line);
        if (!connection_ok) {
            connection_ok = line.startsWith("HTTP/1.1 200");
        }
        if (line == "\r") {
            Serial.println("headers received");
            break;
        }
    }
    if (!connection_ok) return;
    // wait for data to be available, if the server is a little slow
    while (!client->available()) {
        delay(1);
    }
    if (read16(client) == 0x4D42) {  // BMP signature
        int32_t fileSize = read32(client);
        int32_t creatorBytes = read32(client);
        int32_t imageOffset = read32(client);  // Start of image data
        int32_t headerSize = read32(client);
        int32_t width = read32(client);
        int32_t height = read32(client);
        uint16_t planes = read16(client);
        uint16_t depth = read16(client);  // bits per pixel
        int32_t format = read32(client);
        int32_t bytes_read = 7 * 4 + 3 * 2;  // read so far
        (void)creatorBytes;
        if ((planes == 1) && ((format == 0) || (format == 3))) {  // uncompressed is handled, 565 also
            Serial.print("File size: ");
            Serial.println(fileSize);
            Serial.print("Image Offset: ");
            Serial.println(imageOffset);
            Serial.print("Header size: ");
            Serial.println(headerSize);
            Serial.print("Bit Depth: ");
            Serial.println(depth);
            Serial.print("Image size: ");
            Serial.print(width);
            Serial.print('x');
            Serial.println(height);
            // BMP rows are padded (if needed) to 4-byte boundary
            uint32_t rowSize = (width * depth / 8 + 3) & ~3;
            if (depth < 8) rowSize = ((width * depth + 8 - depth) / 8 + 3) & ~3;
            if (height < 0) {
                height = -height;
                flip = false;
            }
            uint16_t w = width;
            uint16_t h = height;
            if ((x + w - 1) >= display.width()) w = display.width() - x;
            if ((y + h - 1) >= display.height()) h = display.height() - y;
            valid = true;
            Serial.print("valid: ");
            Serial.println(valid);
            uint8_t bitmask = 0xFF;
            uint8_t bitshift = 8 - depth;
            uint16_t red, green, blue;
            bool whitish = false;
            bool colored = false;
            if (depth == 1) with_color = false;
            if (depth <= 8) {
                if (depth < 8) bitmask >>= depth;
                // bytes_read += skip(client, 54 - bytes_read); //palette is
                // always @ 54
                bytes_read += skip(client, imageOffset - (4 << depth) - bytes_read);  // 54 for regular, diff
                                                                                      // for colorsimportant
                for (uint16_t pn = 0; pn < (1 << depth); pn++) {
                    blue = client->read();
                    green = client->read();
                    red = client->read();
                    client->read();
                    bytes_read += 4;
                    whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80);  // whitish
                    colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));                                                   // reddish or yellowish?
                    if (0 == pn % 8) mono_palette_buffer[pn / 8] = 0;
                    mono_palette_buffer[pn / 8] |= whitish << pn % 8;
                    if (0 == pn % 8) color_palette_buffer[pn / 8] = 0;
                    color_palette_buffer[pn / 8] |= colored << pn % 8;
                }
            }
            uint32_t rowPosition = flip ? imageOffset + (height - h) * rowSize : imageOffset;
            // Serial.print("skip "); Serial.println(rowPosition - bytes_read);
            bytes_read += skip(client, rowPosition - bytes_read);
            for (uint16_t row = 0; row < h; row++, rowPosition += rowSize)  // for each line
            {
                if (!connection_ok || !(client->connected() || client->available())) break;
                delay(1);  // yield() to avoid WDT
                uint32_t in_remain = rowSize;
                uint32_t in_idx = 0;
                uint32_t in_bytes = 0;
                uint8_t in_byte = 0;  // for depth <= 8
                uint8_t in_bits = 0;  // for depth <= 8
                uint16_t color = GxEPD_WHITE;
                for (uint16_t col = 0; col < w; col++)  // for each pixel
                {
                    yield();
                    if (!connection_ok || !(client->connected() || client->available())) break;
                    // Time to read more pixel data?
                    if (in_idx >= in_bytes)  // ok, exact match for 24bit
                                             // also (size IS multiple of 3)
                    {
                        uint32_t get = in_remain > sizeof(input_buffer) ? sizeof(input_buffer) : in_remain;
                        uint32_t got = read8n(client, input_buffer, get);
                        while ((got < get) && connection_ok) {
                            // Serial.print("got ");
                            // Serial.print(got);
                            // Serial.print(" < ");
                            // Serial.print(get);
                            // Serial.print(" @ ");
                            // Serial.println(bytes_read);
                            uint32_t gotmore = read8n(client, input_buffer + got, get - got);
                            got += gotmore;
                            connection_ok = gotmore > 0;
                        }
                        in_bytes = got;
                        in_remain -= got;
                        bytes_read += got;
                    }
                    if (!connection_ok) {
                        Serial.print("Error: got no more after ");
                        Serial.print(bytes_read);
                        Serial.println(" bytes read!");
                        break;
                    }
                    switch (depth) {
                        case 24:
                            blue = input_buffer[in_idx++];
                            green = input_buffer[in_idx++];
                            red = input_buffer[in_idx++];
                            whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80);  // whitish
                            colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));  // reddish or yellowish?
                            break;
                        case 16: {
                            uint8_t lsb = input_buffer[in_idx++];
                            uint8_t msb = input_buffer[in_idx++];
                            if (format == 0)  // 555
                            {
                                blue = (lsb & 0x1F) << 3;
                                green = ((msb & 0x03) << 6) | ((lsb & 0xE0) >> 2);
                                red = (msb & 0x7C) << 1;
                            } else  // 565
                            {
                                blue = (lsb & 0x1F) << 3;
                                green = ((msb & 0x07) << 5) | ((lsb & 0xE0) >> 3);
                                red = (msb & 0xF8);
                            }
                            whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80);  // whitish
                            colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));  // reddish or yellowish?
                        } break;
                        case 1:
                        case 4:
                        case 8: {
                            if (0 == in_bits) {
                                in_byte = input_buffer[in_idx++];
                                in_bits = 8;
                            }
                            uint16_t pn = (in_byte >> bitshift) & bitmask;
                            whitish = mono_palette_buffer[pn / 8] & (0x1 << pn % 8);
                            colored = color_palette_buffer[pn / 8] & (0x1 << pn % 8);
                            in_byte <<= depth;
                            in_bits -= depth;
                        } break;
                    }
                    if (whitish) {
                        color = GxEPD_WHITE;
                    } else if (colored && with_color) {
                        color = GxEPD_RED;
                    } else {
                        color = GxEPD_BLACK;
                    }
                    uint16_t yrow = y + (flip ? h - row - 1 : row);
                    display.drawPixel(x + col, yrow, color);
                }  // end pixel
            }      // end line
        }
        Serial.print("bytes read ");
        Serial.println(bytes_read);
    }
    if (!valid) {
        Serial.println("bitmap format not handled.");
    }
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
    char path[128];
    snprintf(path, 128, "/forecast-img/%.3f,%.3f", lat, lon);
    draw_bitmap_http(SERVER_HOST, SERVER_PORT, path, 0, 0, false);
}

void loop() {
    clearScreen();
    render_time();
    render_weather();
    display.display();
    display.hibernate();
    delay(1000 * 60 * REFRESH_RATE_MINS);
};
