#include <Arduino.h>
#include <TFT_eSPI.h>

#define TFT_BL 21

TFT_eSPI tft;

void setup()
{
    Serial.begin(115200);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(0);

    Serial.printf("Width=%d Height=%d\n",
                  tft.width(),
                  tft.height());

    tft.fillScreen(TFT_RED);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.drawString("0,0", 0, 0, 4);

    tft.drawString("160,120", 160, 120, 2);

    tft.drawString("0,239", 0, 220, 2);

    tft.drawString("319,239", 220, 220, 2);

    tft.drawPixel(0, 0, TFT_RED);
    tft.drawPixel(319, 0, TFT_GREEN);
    tft.drawPixel(0, 239, TFT_BLUE);
    tft.drawPixel(319, 239, TFT_YELLOW);

    tft.drawFastHLine(0, 120, 320, TFT_RED);
    tft.drawFastVLine(160, 0, 240, TFT_GREEN);



    tft.fillScreen(TFT_BLACK);

    tft.fillRect(0,   0, 80, 80, TFT_RED);
    tft.fillRect(80,  0, 80, 80, TFT_GREEN);
    tft.fillRect(160, 0, 80, 80, TFT_BLUE);
    tft.fillRect(240, 0, 80, 80, TFT_WHITE);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("RED",   0,   90, 2);
    tft.drawString("GREEN", 80,  90, 2);
    tft.drawString("BLUE",  160, 90, 2);
    tft.drawString("WHITE", 240, 90, 2);

    uint32_t start = micros();

    for (int i = 0; i < 100; i++)
    {
        tft.fillScreen(TFT_BLACK);
    }

    uint32_t elapsed = micros() - start;

    Serial.printf("Average fillScreen = %.2f ms\n",
                elapsed / 1000.0 / 100.0);


}

void loop()
{
}