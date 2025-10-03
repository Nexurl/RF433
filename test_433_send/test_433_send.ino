#include <RH_ASK.h>
#include <SPI.h> // Not required, but included for compatibility

// Create ASK object
RH_ASK rf_driver;

void setup()
{
    pinMode(13, OUTPUT);
    digitalWrite(13, HIGH);
    Serial.begin(9600); // Start serial communication
    if (!rf_driver.init())
        Serial.println("init failed");
}

void loop()
{
    const char *msg = "Hello World";
    rf_driver.send((uint8_t *)msg, strlen(msg));
    rf_driver.waitPacketSent();
    delay(1000); // Wait for a second before next transmission
}