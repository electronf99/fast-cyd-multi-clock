#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

// CYD backlight and SD card pins
#define TFT_BL 21
#define SD_CS 5

TFT_eSPI tft;

// Number of display lines transferred at a time
constexpr int CHUNK_HEIGHT = 46;

// PWM channel used for backlight control
constexpr int BACKLIGHT_CHANNEL = 0;

// Buffer used for SD/LittleFS transfers and TFT drawing
uint16_t buffer[320 * CHUNK_HEIGHT];

//
// Set TFT backlight brightness
// 0   = off
// 255 = full brightness
//
void setBrightness(uint8_t brightness)
{
    ledcWrite(BACKLIGHT_CHANNEL, brightness);
}

//
// Fade the display down.
//
// This is intentionally quite slow to
// simulate a nixie tube cooling down.
//
void fadeOut(int maxValue, int minValue, int delayValue)
{
    for (int b = maxValue; b >= minValue; b -= 3)
    {
        setBrightness(b);
        delay(delayValue);
    }
}

//
// Fade the display up.
//
// This creates a nice "tube warming up"
// visual effect.
//
void fadeIn(int minValue, int maxValue)
{
    for (int b = minValue; b <= maxValue; b += 1)
    {
        setBrightness(b);
        delay(5);
    }
}

//
// Copy a raw image from SD card
// into the LittleFS cache.
//
// The current image remains visible
// while this happens.
//
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

//
// Draw an image stored in LittleFS.
//
// The screen fades down,
// redraw happens while dark,
// then the image slowly "warms up"
// and settles.
//
// This completely hides the redraw
// and produces a convincing nixie effect.
//
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

    // Cool down the old digit
    fadeOut(200, 0, 10);

    // Draw new image while dark
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

    // Warm up the new digit
    fadeIn(30, 245);

    // Let it cool slightly
    // so it settles naturally
    fadeOut(255, 250, 30);

    f.close();
}

void setup()
{
    Serial.begin(115200);

    //
    // Configure backlight PWM
    //
    ledcSetup(BACKLIGHT_CHANNEL, 1000, 8);
    ledcAttachPin(TFT_BL, BACKLIGHT_CHANNEL);

    // Start at full brightness
    ledcWrite(BACKLIGHT_CHANNEL, 245);

    //
    // TFT setup
    //
    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);

    //
    // LittleFS setup
    //
    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS failed");

        while (true)
            delay(1000);
    }

    //
    // SD card setup
    //
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
    static int digit = 0;

    char filename[32];

    snprintf(
        filename,
        sizeof(filename),
        "/nixie-%d.raw",
        digit);

    Serial.printf("Loading %s\n", filename);

    //
    // Copy new image into cache
    //
    copySdToLittleFS(
        filename,
        "/current.raw");

    //
    // Display image with
    // nixie warm-up effect
    //
    drawLittleFSImage(
        "/current.raw");

    digit++;

    if (digit > 9)
    {
        digit = 0;
    }

    delay(10);
}