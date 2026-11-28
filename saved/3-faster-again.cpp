#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>


#include <esp_heap_caps.h>


/*
    ============================================================
    CYD NIXIE DISPLAY
    ============================================================

    This display receives data over UART from a master device.

    Example packets:

        <nixie>
        [01088]

    Style packets:
        <nixie>
        <vfd>
        <flip>

    Digit packets:
        [01088]
        [111C8]

    The style packet selects which image set to use.

    The digit packet contains five display positions.

    Example:

        <nixie>
        [01088]

    If DIGITNUM == 0:
        Displays nixie-0.raw

    If DIGITNUM == 1:
        Displays nixie-1.raw

    If DIGITNUM == 4:
        Displays nixie-8.raw

    ============================================================

    Image files on SD card:

        nixie-0.raw
        nixie-1.raw
        ...
        nixie-9.raw
        nixie-C.raw

        vfd-0.raw
        vfd-1.raw
        ...

    ============================================================

    DIGITNUM file on SD card:

        /DIGITNUM

    Contents:

        0
        1
        2
        3
        4

    This allows all CYDs to run the same firmware.

    ============================================================

    Display process:

        SD Card
            ↓
        LittleFS Cache
            ↓
        Display

    The current image remains visible while the next image
    is copied from SD card into LittleFS.

    Once cached:

        1. Fade old digit out
        2. Draw new image
        3. Fade digit in
        4. Slightly cool down

    This completely hides the redraw.
*/

//
// CYD pin definitions
//
#define TFT_BL 21
#define SD_CS 5

//
// TFT display object
//
TFT_eSPI tft;

//
// Which digit position this display shows.
//
// Loaded from the SD card file:
//
//     /DIGITNUM
//
int DIGITNUM = 0;

//
// Number of display lines transferred each pass.
//
// 46 was found experimentally to perform best.
//
constexpr int CHUNK_HEIGHT = 210;

//
// PWM channel used for backlight control.
//
constexpr int BACKLIGHT_CHANNEL = 0;

//
// Shared buffer.
//
// Used both for:
//
//     SD -> LittleFS copy
//
// and:
//
//     LittleFS -> TFT drawing
//
uint16_t buffer[240 * CHUNK_HEIGHT];

//
// Prevents unnecessary redraws.
//
char currentDigit = '\0';

//
// Active display style.
//
// Defaults to "nixie" until a style packet arrives.
//
char currentStyle[32] = "nixie";

//
// Digit packet buffer.
//
// Example:
//
//     [01088]
//
char packet[8];
int packetPos = 1;

//
// Style packet buffer.
//
// Example:
//
//     <nixie>
//
char styleBuffer[32];
int stylePos = 0;

bool receivingStyle = false;

//
// Read digit position from SD card.
//
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

    DIGITNUM = s.toInt() - 1;

    Serial.printf(
        "Digit position = %d\n",
        DIGITNUM );

    f.close();
}

//
// Set TFT backlight brightness.
//
// 0   = off
// 255 = full brightness
//
void setBrightness(uint8_t brightness)
{
    ledcWrite(BACKLIGHT_CHANNEL, brightness);
}

//
// Fade brightness down.
//
// Used to simulate a nixie tube cooling.
//
void fadeOut(
    int maxValue,
    int minValue,
    int delayValue)
{
    for (int b = maxValue; b >= minValue; b -= 3)
    {
        setBrightness(b);
        delay(delayValue);
    }
}

//
// Fade brightness up.
//
// Used to simulate a nixie tube warming.
//
void fadeIn(
    int minValue,
    int maxValue)
{
    for (int b = minValue; b <= maxValue; b += 1)
    {
        setBrightness(b);
        delay(5);
    }
}

//
// Copy a raw image from SD card into
// LittleFS cache.
//
// The current image stays visible while
// this happens.
//
void copySdToLittleFS(
    const char *src,
    const char *dst)
{
    File in = SD.open(src);

    if (!in)
    {
        Serial.printf(
            "Cannot open SD file %s\n",
            src);
        return;
    }

    LittleFS.remove(dst);

    File out = LittleFS.open(dst, "w");

    if (!out)
    {
        Serial.printf(
            "Cannot create LittleFS file %s\n",
            dst);

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
// Draw image from LittleFS.
//
// Since the CYD doesn't have enough RAM
// to hold a complete framebuffer, the
// image is drawn in chunks while the
// display is dark.
//
// The redraw is therefore invisible.
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

    for (int y = 0; y < 320; y += CHUNK_HEIGHT)
    {
        int lines =
            min(CHUNK_HEIGHT, 320 - y);

        int bytesRead =
            f.read(
                (uint8_t *)buffer,
                240 * lines * 2);

        if (bytesRead != (240 * lines * 2))
        {
            Serial.println("Short read");
            break;
        }

        tft.pushImage(
            0,
            y,
            240,
            lines,
            buffer);
    }

    f.close();
}



//
// Load and display a digit.
//
void showDigit(char digit)
{
    if (digit == currentDigit)
        return;

    char filename[64];

    snprintf(
        filename,
        sizeof(filename),
        "/%s-%c.raw",
        currentStyle,
        digit);

    Serial.printf(
        "Loading %s\n",
        filename);

    copySdToLittleFS(
        filename,
        "/current.raw");

    drawLittleFSImage(
        "/current.raw");

    currentDigit = digit;
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.printf(
        "Free heap = %u\n",
        ESP.getFreeHeap());

    Serial.printf(
        "Largest block = %u\n",
        heap_caps_get_largest_free_block(
            MALLOC_CAP_8BIT));


    //
    // Backlight PWM
    //
    ledcSetup(
        BACKLIGHT_CHANNEL,
        1000,
        8);

    ledcAttachPin(
        TFT_BL,
        BACKLIGHT_CHANNEL);

    //
    // Initial brightness
    //
    ledcWrite(
        BACKLIGHT_CHANNEL,
        245);

    //
    // TFT setup
    //
    tft.init();
    tft.setRotation(2);
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

    //
    // Load this display's digit number
    //
    loadDigitPosition();

    //
    // UART from master controller
    //
    Serial2.begin(
        115200,
        SERIAL_8N1,
        22,   // RX
        27);  // TX

    Serial.println("Starting...");
}

void loop()
{
    while (Serial2.available())
    {
        char c = Serial2.read();

        //
        // Start of style packet
        //
        if (c == '<')
        {
            stylePos = 0;
            receivingStyle = true;
        }

        //
        // End of style packet
        //
        else if (c == '>')
        {
            styleBuffer[stylePos] = '\0';

            strcpy(
                currentStyle,
                styleBuffer);

            Serial.printf(
                "Style changed to: %s\n",
                currentStyle);

            receivingStyle = false;
        }

        //
        // Collect style name
        //
        else if (receivingStyle)
        {
            if (stylePos < sizeof(styleBuffer) - 1)
            {
                styleBuffer[stylePos++] = c;
            }
        }

        //
        // Start of digit packet
        //
        else if (c == '[')
        {
            packetPos = 0;
        }

        //
        // End of digit packet
        //
        else if (c == ']')
        {
            packet[packetPos] = '\0';

            Serial.printf(
                "Packet: %s\n",
                packet);

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

        //
        // Collect packet data
        //
        else
        {
            if (packetPos < sizeof(packet) - 1)
            {
                packet[packetPos++] = c;
            }
        }
    }
}