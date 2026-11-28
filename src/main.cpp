/******************************************************************************
 *
 * CYD NETWORK DIGIT DISPLAY
 * =========================
 *
 * Purpose
 * -------
 * This firmware runs on a Cheap Yellow Display (ESP32 + ILI9341 TFT) and
 * displays a single digit received from a master controller via UART.
 *
 * Multiple displays can be configured with different DIGITNUM values so that
 * each display shows a different position from the same incoming packet.
 *
 * Example:
 *
 *     Incoming packet:
 *         [01234]
 *
 *     Display with DIGITNUM=0 shows:
 *         0
 *
 *     Display with DIGITNUM=1 shows:
 *         1
 *
 *     Display with DIGITNUM=2 shows:
 *         2
 *
 * UART Protocol
 * -------------
 *
 * Digit packet:
 *
 *     [01234]
 *
 * Style packet:
 *
 *     <nixie>
 *     <led>
 *     <lcd>
 *
 * Display Styles
 * --------------
 *
 * Each style consists of 11 full-screen RGB565 RAW images:
 *
 *     style-0.raw
 *     style-1.raw
 *     style-2.raw
 *     style-3.raw
 *     style-4.raw
 *     style-5.raw
 *     style-6.raw
 *     style-7.raw
 *     style-8.raw
 *     style-9.raw
 *     style-C.raw
 *
 * Examples:
 *
 *     /nixie-0.raw
 *     /nixie-1.raw
 *     ...
 *     /led-0.raw
 *     /led-1.raw
 *     ...
 *
 * DIGITNUM Configuration
 * ----------------------
 *
 * The SD card contains a file:
 *
 *     /DIGITNUM
 *
 * containing a value from 1 to 5.
 *
 * Example:
 *
 *     3
 *
 * This display will show the third digit from every received packet.
 *
 * Display Performance Strategy
 * ----------------------------
 *
 * The objective is to make full-screen image changes appear instantaneous.
 *
 * To achieve this:
 *
 *     SD Card
 *         ↓
 *     LittleFS Cache
 *         ↓
 *     Internal RAM Framebuffers
 *         ↓
 *     TFT Display @ 80MHz SPI
 *
 * Images are copied from SD card to LittleFS once during startup or
 * when a style change occurs.
 *
 * During normal operation:
 *
 *     - No SD card access is required.
 *     - Images are read from LittleFS.
 *     - Entire images are loaded into RAM before display.
 *     - TFT updates use large continuous SPI transfers.
 *
 * Memory Layout
 * -------------
 *
 * A 240x320 RGB565 image requires:
 *
 *     240 × 320 × 2
 *     = 153,600 bytes
 *
 * Instead of allocating one large framebuffer, the image is split into
 * two halves:
 *
 *     Top half:
 *         240 × 160 × 2
 *         = 76,800 bytes
 *
 *     Bottom half:
 *         240 × 160 × 2
 *         = 76,800 bytes
 *
 * Total framebuffer memory:
 *
 *         153,600 bytes
 *
 * Bluetooth memory is released at startup to maximise available internal RAM.
 *
 * Startup Sequence
 * ----------------
 *
 *     1. Release Bluetooth memory.
 *     2. Allocate dual framebuffers.
 *     3. Initialise TFT.
 *     4. Mount LittleFS.
 *     5. Mount SD card.
 *     6. Read DIGITNUM configuration.
 *     7. Display nixie-bg.raw startup image.
 *     8. Cache active style from SD to LittleFS.
 *     9. Start UART communications.
 *    10. Wait for digit packets.
 *
 * Display Update Process
 * ----------------------
 *
 * When a digit changes:
 *
 *     1. Required image is loaded from LittleFS.
 *     2. Image is read into RAM buffers.
 *     3. Top half transferred to TFT.
 *     4. Bottom half transferred to TFT.
 *     5. Display updates in approximately 18ms.
 *
 * Repeated digits are ignored to prevent unnecessary redraws.
 *
 * Hardware
 * --------
 *
 * Board:
 *     ESP32 Cheap Yellow Display (CYD)
 *
 * Display:
 *     ILI9341
 *
 * Resolution:
 *     240 x 320
 *
 * Colour Format:
 *     RGB565
 *
 * SPI Clock:
 *     80 MHz
 *
 * Storage:
 *     SD Card + LittleFS
 *
 * Serial Interface:
 *     UART2
 *
 * Performance
 * -----------
 *
 * Typical full-screen refresh:
 *
 *     ~18 ms TFT transfer time
 *
 * Result:
 *
 *     Full-screen image transitions appear visually instantaneous.
 *
 ******************************************************************************/

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

#include <esp_bt.h>
#include <esp_heap_caps.h>

//
// CYD pin definitions
//
#define TFT_BL 21
#define SD_CS 5

//
// Display dimensions & dual-buffer split constants
//
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 320;
constexpr int HALF_H = SCREEN_H / 2;
constexpr size_t HALF_BYTES = SCREEN_W * HALF_H * sizeof(uint16_t);

//
// All 11 valid digit characters for each style
//
constexpr char DIGIT_CHARS[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'C'};
constexpr int NUM_DIGITS = sizeof(DIGIT_CHARS) / sizeof(DIGIT_CHARS[0]);

//
// TFT display object
//
TFT_eSPI tft;

//
// Two 75 KB framebuffers
//
uint16_t *bufTop = nullptr;
uint16_t *bufBot = nullptr;

//
// Which digit position this display shows
//
int DIGITNUM = 0;

//
// PWM channel used for backlight control
//
constexpr int BACKLIGHT_CHANNEL = 0;

//
// State tracking
//
char currentDigit = '\0';
char currentStyle[32] = "nixie";

//
// UART packet buffers
//
char packet[8];
int packetPos = 0;
bool receivingDigits = false;

char styleBuffer[32];
int stylePos = 0;
bool receivingStyle = false;

//
// Allocate dual framebuffers
//
void initFrameBuffers()
{
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

    bufTop = (uint16_t *)heap_caps_malloc(
        HALF_BYTES,
        MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    bufBot = (uint16_t *)heap_caps_malloc(
        HALF_BYTES,
        MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    if (!bufTop || !bufBot)
    {
        Serial.println("FATAL: Failed to allocate dual framebuffers!");
        while (true)
            delay(1000);
    }
}

//
// Read digit position from SD
//
void loadDigitPosition()
{
    File f = SD.open("/DIGITNUM");

    if (!f)
    {
        Serial.println("DIGITNUM not found on sdcard, defaulting to 0");
        return;
    }

    String s = f.readStringUntil('\n');
    s.trim();

    DIGITNUM = constrain(s.toInt() - 1, 0, 4);

    Serial.printf("Digit position = %d\n", DIGITNUM);

    f.close();
}

//
// Backlight brightness
//
void setBrightness(uint8_t brightness)
{
    ledcWrite(BACKLIGHT_CHANNEL, brightness);
}

//
// Show a full-screen RAW image directly from SD
//
void drawRawFromSD(const char *filename)
{
    File f = SD.open(filename);

    if (!f)
    {
        Serial.printf("Cannot open %s\n", filename);
        return;
    }

    size_t r1 = f.read((uint8_t *)bufTop, HALF_BYTES);
    size_t r2 = f.read((uint8_t *)bufBot, HALF_BYTES);

    f.close();

    if (r1 != HALF_BYTES || r2 != HALF_BYTES)
    {
        Serial.printf("Short read from %s\n", filename);
        return;
    }

    uint32_t tStart = micros();

    tft.startWrite();
    tft.pushImage(0, 0, SCREEN_W, HALF_H, bufTop);
    tft.pushImage(0, HALF_H, SCREEN_W, HALF_H, bufBot);
    tft.endWrite();

    Serial.printf(
        "Displayed %s in %.2f ms\n",
        filename,
        (micros() - tStart) / 1000.0f);
}

//
// Copy a raw image from SD to LittleFS
//
bool copySdToLittleFS(const char *src, const char *dst)
{
    File in = SD.open(src);

    if (!in)
    {
        Serial.printf("Cannot open SD file %s\n", src);
        return false;
    }

    LittleFS.remove(dst);

    File out = LittleFS.open(dst, "w");

    if (!out)
    {
        Serial.printf("Cannot create LittleFS file %s\n", dst);
        in.close();
        return false;
    }

    constexpr size_t COPY_CHUNK = 16384;

    while (in.available())
    {
        int bytesRead = in.read(
            (uint8_t *)bufTop,
            COPY_CHUNK);

        if (bytesRead > 0)
        {
            if (out.write(
                    (uint8_t *)bufTop,
                    bytesRead) != (size_t)bytesRead)
            {
                Serial.printf("Write failed for %s\n", dst);

                out.close();
                in.close();

                return false;
            }
        }
    }

    out.close();
    in.close();

    return true;
}

//
// Cache style to LittleFS
//
void cacheStyleImages(const char *style)
{
    uint32_t tStart = millis();

    Serial.printf(
        "Pre-caching style '%s' to LittleFS...\n",
        style);

    char srcPath[64];
    char dstPath[32];

    for (int i = 0; i < NUM_DIGITS; i++)
    {
        char d = DIGIT_CHARS[i];

        snprintf(
            srcPath,
            sizeof(srcPath),
            "/%s-%c.raw",
            style,
            d);

        snprintf(
            dstPath,
            sizeof(dstPath),
            "/cache-%c.raw",
            d);

        if (copySdToLittleFS(srcPath, dstPath))
        {
            Serial.printf(
                "  Cached %s -> %s\n",
                srcPath,
                dstPath);
        }
    }

    Serial.printf(
        "Style cache complete in %lu ms\n",
        millis() - tStart);
}

//
// Draw image from LittleFS
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

    size_t r1 = f.read((uint8_t *)bufTop, HALF_BYTES);
    size_t r2 = f.read((uint8_t *)bufBot, HALF_BYTES);

    f.close();

    if (r1 != HALF_BYTES || r2 != HALF_BYTES)
    {
        Serial.printf(
            "Short read from LittleFS: r1=%u r2=%u\n",
            r1,
            r2);
        return;
    }

    uint32_t tStart = micros();

    tft.startWrite();
    tft.pushImage(0, 0, SCREEN_W, HALF_H, bufTop);
    tft.pushImage(0, HALF_H, SCREEN_W, HALF_H, bufBot);
    tft.endWrite();

    uint32_t elapsedUs = micros() - tStart;

    Serial.printf(
        "Draw complete in %lu us (%.2f ms)\n",
        elapsedUs,
        elapsedUs / 1000.0f);
}

//
// Show digit
//
void showDigit(char digit, bool forceRedraw = false)
{
    if (!forceRedraw && digit == currentDigit)
        return;

    char cachePath[32];

    snprintf(
        cachePath,
        sizeof(cachePath),
        "/cache-%c.raw",
        digit);

    drawLittleFSImage(cachePath);

    currentDigit = digit;
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    initFrameBuffers();

    ledcSetup(BACKLIGHT_CHANNEL, 1000, 8);
    ledcAttachPin(TFT_BL, BACKLIGHT_CHANNEL);

    setBrightness(245);

    tft.init();
    tft.setRotation(2);
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);

    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS failed");

        while (true)
            delay(1000);
    }

    Serial.printf(
        "LittleFS total=%u bytes, used=%u bytes\n",
        LittleFS.totalBytes(),
        LittleFS.usedBytes());

    if (!SD.begin(SD_CS))
    {
        Serial.println("SD failed");

        while (true)
            delay(1000);
    }

    loadDigitPosition();

    //
    // Show background immediately
    //
    drawRawFromSD("/nixie-bg.raw");

    //
    // Cache default style
    //
    cacheStyleImages(currentStyle);

    Serial2.begin(
        115200,
        SERIAL_8N1,
        22,
        27);

    Serial.println("Starting...");
}

void loop()
{
    while (Serial2.available())
    {
        char c = Serial2.read();

        if (c == '<')
        {
            stylePos = 0;
            receivingStyle = true;
            receivingDigits = false;
        }
        else if (c == '>' && receivingStyle)
        {
            styleBuffer[stylePos] = '\0';

            if (strcmp(currentStyle, styleBuffer) != 0)
            {
                strcpy(currentStyle, styleBuffer);

                Serial.printf(
                    "Style changed to: %s\n",
                    currentStyle);

                cacheStyleImages(currentStyle);

                if (currentDigit != '\0')
                {
                    showDigit(currentDigit, true);
                }
            }

            receivingStyle = false;
        }
        else if (receivingStyle)
        {
            if (stylePos < (int)sizeof(styleBuffer) - 1)
            {
                styleBuffer[stylePos++] = c;
            }
        }
        else if (c == '[')
        {
            packetPos = 0;
            receivingDigits = true;
        }
        else if (c == ']' && receivingDigits)
        {
            packet[packetPos] = '\0';
            receivingDigits = false;

            if (DIGITNUM >= 0 && DIGITNUM < packetPos)
            {
                showDigit(packet[DIGITNUM]);
            }
        }
        else if (receivingDigits)
        {
            if (packetPos < (int)sizeof(packet) - 1)
            {
                packet[packetPos++] = c;
            }
        }
    }
}