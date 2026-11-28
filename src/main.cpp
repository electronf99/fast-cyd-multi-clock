#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

// CYD backlight and SD card pins
#define TFT_BL 21
#define SD_CS 5

TFT_eSPI tft;

// Which digit in [00558] to display
// 0..4
int DIGITNUM = 0;

// Number of display lines transferred at a time
constexpr int CHUNK_HEIGHT = 46;

// PWM channel used for backlight control
constexpr int BACKLIGHT_CHANNEL = 0;

// Buffer used for SD/LittleFS transfers and TFT drawing
uint16_t buffer[320 * CHUNK_HEIGHT];

char currentDigit = '\0';

char packet[8];
int packetPos = 0;


int digitPosition = 0;

void loadDigitPosition()
{
    File f = SD.open("/DIGITNUM");

    if (!f)
    {
        Serial.println("DIGITNUM not found on sdcard");
        return;
    }

    String s = f.readStringUntil('\n');
    s.trim();

    digitPosition = s.toInt();

    Serial.printf(
        "Digit position = %d\n",
        digitPosition);

    f.close();
}


void setBrightness(uint8_t brightness)
{
    ledcWrite(BACKLIGHT_CHANNEL, brightness);
}

void fadeOut(int maxValue, int minValue, int delayValue)
{
    for (int b = maxValue; b >= minValue; b -= 3)
    {
        setBrightness(b);
        delay(delayValue);
    }
}

void fadeIn(int minValue, int maxValue)
{
    for (int b = minValue; b <= maxValue; b += 1)
    {
        setBrightness(b);
        delay(5);
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

    fadeOut(240, 0, 10);

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

    fadeIn(30, 255);

    fadeOut(255, 254, 30);

    f.close();
}

void showDigit(char digit)
{
    if (digit == currentDigit)
        return;

    currentDigit = digit;

    char filename[32];

    snprintf(
        filename,
        sizeof(filename),
        "/nixie-%c.raw",
        digit);

    Serial.printf("Loading %s\n", filename);

    copySdToLittleFS(
        filename,
        "/current.raw");

    drawLittleFSImage(
        "/current.raw");
}

void setup()
{
    Serial.begin(115200);

    ledcSetup(BACKLIGHT_CHANNEL, 1000, 8);
    ledcAttachPin(TFT_BL, BACKLIGHT_CHANNEL);

    ledcWrite(BACKLIGHT_CHANNEL, 245);

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
    
    loadDigitPosition();
    
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

        if (c == '[')
        {
            packetPos = 0;
        }
        else if (c == ']')
        {
            packet[packetPos] = '\0';

            Serial.printf("Packet: %s\n", packet);

            if (strlen(packet) == 5)
            {
                char digit =
                    packet[DIGITNUM];

                Serial.printf(
                    "Displaying digit: %c\n",
                    digit);

                showDigit(digit);
            }

            packetPos = 0;
        }
        else
        {
            if (packetPos < sizeof(packet) - 1)
            {
                packet[packetPos++] = c;
            }
        }
    }
}