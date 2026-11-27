#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

#define TFT_BL 21
#define SD_CS 5

TFT_eSPI tft;

constexpr int CHUNK_HEIGHT = 46;
constexpr int BACKLIGHT_CHANNEL = 0;

uint16_t buffer[320 * CHUNK_HEIGHT];

void setBrightness(uint8_t brightness)
{
    ledcWrite(BACKLIGHT_CHANNEL, brightness);
}

void fadeOut()
{
    for (int b = 255; b >= 0; b -= 1)
    {
        setBrightness(b);
        delay(1);
    }
}

void fadeIn()
{
    for (int b = 0; b <= 255; b += 3)
    {
        setBrightness(b);
        delay(1);
    }
}

void copySdToLittleFS(const char *src, const char *dst)
{
    File in = SD.open(src);

    if (!in)
    {
        Serial.printf("Cannot open SD file %s\n", src);
        return;
    }

    LittleFS.remove(dst);

    File out = LittleFS.open(dst, "w");

    if (!out)
    {
        Serial.printf("Cannot create LittleFS file %s\n", dst);
        in.close();
        return;
    }

    while (in.available())
    {
        int bytesRead =
            in.read(
                (uint8_t *)buffer,
                sizeof(buffer));

        if (bytesRead > 0)
        {
            out.write(
                (uint8_t *)buffer,
                bytesRead);
        }
    }

    out.close();
    in.close();
}

void drawLittleFSImage(const char *filename)
{
    File f = LittleFS.open(filename, "r");

    if (!f)
    {
        Serial.printf(
            "Cannot open LittleFS file %s\n",
            filename);
        return;
    }

    fadeOut();

    for (int y = 0; y < 240; y += CHUNK_HEIGHT)
    {
        int lines = min(CHUNK_HEIGHT, 240 - y);

        int bytesRead =
            f.read(
                (uint8_t *)buffer,
                320 * lines * 2);

        if (bytesRead != (320 * lines * 2))
        {
            Serial.println("Short read");
            break;
        }

        tft.pushImage(
            0,
            y,
            320,
            lines,
            buffer);
    }

    delay(5);

    fadeIn();

    f.close();
}

void setup()
{
    Serial.begin(115200);

    ledcSetup(BACKLIGHT_CHANNEL, 1000, 8);
    ledcAttachPin(TFT_BL, BACKLIGHT_CHANNEL);
    ledcWrite(BACKLIGHT_CHANNEL, 255);

    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);

    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS failed");

        while (true)
            delay(1000);
    }

    if (!SD.begin(SD_CS))
    {
        Serial.println("SD failed");

        while (true)
            delay(1000);
    }

    Serial.println("Starting...");
}

void loop()
{
    Serial.println("Loading nixie-0");

    copySdToLittleFS(
        "/nixie-0.raw",
        "/current.raw");

    drawLittleFSImage(
        "/current.raw");

    delay(1000);

    Serial.println("Loading nixie-1");

    copySdToLittleFS(
        "/nixie-1.raw",
        "/current.raw");

    drawLittleFSImage(
        "/current.raw");

    delay(1000);
}
