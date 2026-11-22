#include <Arduino.h>

void setup()
{
    Serial.begin(115200);
    Serial.println("Hello World!");
}

void loop()
{
    static int counter = 0;

    Serial.printf("Counter = %d\n", counter++);
    delay(1000);
}