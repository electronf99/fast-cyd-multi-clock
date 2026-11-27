#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>

#define TFT_BL 21
#define SD_CS 5

TFT_eSPI tft;

uint16_t buffer[320 * 16];

void setup()
{
    Serial.begin(115200);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(2);

    if (!SD.begin(SD_CS))
    {
        Serial.println("SD init failed");
        while (true)
            delay(1000);
    }

    Serial.println("SD init OK");

    File f = SD.open("/0.raw");

    if (!f)
    {
        Serial.println("Cannot open file");
        while (true)
            delay(1000);
    }

    uint32_t start = millis();

    for (int y = 0; y < 240; y += 16)
    {
        int bytesRead = f.read(
            (uint8_t *)buffer,
            sizeof(buffer));
            Serial.println("----------")
        if (bytesRead != sizeof(buffer))
        {
            Serial.printf("Short read at line %d\n", y);
            break;
        }

        tft.pushImage(
            0,
            y,
            320,
            16,
            buffer);
    }

    f.close();

    Serial.printf("Draw took %lu ms\n",
                  millis() - start);
}

void loop()
{
}