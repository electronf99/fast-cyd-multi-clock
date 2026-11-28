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
constexpr int HALF_H = SCREEN_H / 2; // 160 rows
constexpr size_t HALF_BYTES = SCREEN_W * HALF_H * sizeof(uint16_t); // 76,800 bytes (75 KB)

//
// TFT display object
//
TFT_eSPI tft;

//
// Two 75 KB framebuffers (allocated in separate internal SRAM banks)
//
uint16_t *bufTop = nullptr;
uint16_t *bufBot = nullptr;

//
// Which digit position this display shows (0..4), loaded from /DIGITNUM
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
char packet[16];
int packetPos = 0;
bool receivingDigits = false;

char styleBuffer[32];
int stylePos = 0;
bool receivingStyle = false;

//
// Allocate two 75 KB buffers in separate SRAM banks at startup
//
void initFrameBuffers()
{
    // Reclaim ~54 KB of unused Bluetooth DRAM so both 75 KB buffers fit cleanly
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

    bufTop = (uint16_t *)heap_caps_malloc(HALF_BYTES, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    bufBot = (uint16_t *)heap_caps_malloc(HALF_BYTES, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    if (!bufTop || !bufBot)
    {
        Serial.println("FATAL: Failed to allocate dual 75 KB framebuffers!");
        while (true)
            delay(1000);
    }
}

//
// Read digit position from SD card (/DIGITNUM contains 1..5)
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
// Set TFT backlight brightness (0 = off, 255 = full)
//
void setBrightness(uint8_t brightness)
{
    ledcWrite(BACKLIGHT_CHANNEL, brightness);
}

//
// Copy a raw image from SD card into LittleFS cache in safe 4 KB chunks
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

    // Reuse the start of bufTop as a 4 KB scratch buffer
    constexpr size_t COPY_CHUNK = 4096;
    while (in.available())
    {
        int bytesRead = in.read((uint8_t *)bufTop, COPY_CHUNK);
        if (bytesRead > 0)
        {
            out.write((uint8_t *)bufTop, bytesRead);
        }
    }

    out.close();
    in.close();
}

//
// Load entire 240x320 image into RAM first, then blast to TFT in one SPI burst
//
void drawLittleFSImage(const char *filename)
{
    File f = LittleFS.open(filename, "r");
    if (!f)
    {
        Serial.printf("Cannot open LittleFS file %s\n", filename);
        return;
    }

    // 1. Read both halves into RAM completely before touching the display
    size_t r1 = f.read((uint8_t *)bufTop, HALF_BYTES);
    size_t r2 = f.read((uint8_t *)bufBot, HALF_BYTES);
    f.close();

    if (r1 != HALF_BYTES || r2 != HALF_BYTES)
    {
        Serial.printf("Short read from LittleFS: r1=%u r2=%u\n", r1, r2);
        return;
    }

    


    uint32_t tStart = micros();

    // 2. Push both halves back-to-back over hardware SPI FIFO (~30 ms total)
    tft.startWrite();
    tft.pushImage(0, 0,      SCREEN_W, HALF_H, bufTop);
    tft.pushImage(0, HALF_H, SCREEN_W, HALF_H, bufBot);
    tft.endWrite();

    uint32_t elapsedUs = micros() - tStart;


    Serial.println("Draw complete");
    
    Serial.printf("Draw complete in %lu us (%.2f ms)\n", elapsedUs, elapsedUs / 1000.0f);
    
}

//
// Load and display a digit
//
void showDigit(char digit, bool forceRedraw = false)
{
    if (!forceRedraw && digit == currentDigit)
        return;

    char filename[64];
    snprintf(filename, sizeof(filename), "/%s-%c.raw", currentStyle, digit);

    Serial.printf("Loading %s\n", filename);

    copySdToLittleFS(filename, "/current.raw");
    drawLittleFSImage("/current.raw");

    currentDigit = digit;
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    // Allocate dual 75 KB buffers FIRST before anything else fragments the heap
    initFrameBuffers();

    Serial.printf("Free heap after FB alloc = %u\n", ESP.getFreeHeap());
    Serial.printf("Largest remaining block  = %u\n",
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    // Backlight PWM
    ledcSetup(BACKLIGHT_CHANNEL, 1000, 8);
    ledcAttachPin(TFT_BL, BACKLIGHT_CHANNEL);
    setBrightness(245);

    // TFT setup (no initDMA needed for pushImage)
    tft.init();
    tft.setRotation(2);
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);

    // LittleFS setup
    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS failed");
        while (true)
            delay(1000);
    }

    // SD card setup
    if (!SD.begin(SD_CS))
    {
        Serial.println("SD failed");
        while (true)
            delay(1000);
    }

    loadDigitPosition();

    // UART from master controller
    Serial2.begin(115200, SERIAL_8N1, 22, 27);
    Serial.println("Starting...");
}

void loop()
{
    while (Serial2.available())
    {
        char c = Serial2.read();

        // Style packet: <nixie>
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
                Serial.printf("Style changed to: %s\n", currentStyle);
                if (currentDigit != '\0')
                {
                    showDigit(currentDigit, true); // Refresh current digit in new style
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
        // Digit packet: [01088]
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
