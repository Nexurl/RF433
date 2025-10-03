#include <RH_ASK.h>
#include <SPI.h> // Not required, but included for compatibility

// Create ASK object
RH_ASK rf_driver;

void setup()
{
    pinMode(13, OUTPUT); // second 5V to supply power to the transmitter
    digitalWrite(13, HIGH);
    Serial.begin(9600); // Start serial communication
    if (!rf_driver.init())
        Serial.println("init failed");
}

void loop()
{
    // Buffer to hold received message
    uint8_t buf[RH_ASK_MAX_MESSAGE_LEN];
    uint8_t buflen = sizeof(buf);
    // Check if a message is available
    if (rf_driver.recv(buf, &buflen)) {
        Serial.print("Received: ");
        for (uint8_t i = 0; i < buflen; i++) {
            if (buf[i] < 16) Serial.print('0');
            Serial.print(buf[i], HEX);
            Serial.print(' ');
        }
        Serial.println();
    }
    delay(1000); // Wait for a second before next check
}