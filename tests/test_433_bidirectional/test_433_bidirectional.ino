#include <RH_ASK.h>
#include <SPI.h> // Not required, but included for compatibility

// Create ASK object
// default pin configuration:
// RH_ASK(uint16_t speed = 2000, uint8_t rxPin = 11, uint8_t txPin = 12, uint8_t pttPin = 10, bool pttInverted = false);
RH_ASK rf_driver;

void setup()
{
    pinMode(13, OUTPUT); // second 5V to supply power to the transmitter
    digitalWrite(13, HIGH);
    Serial.begin(9600); // Start serial communication
    if (!rf_driver.init())
        Serial.println("init failed");
    Serial.println("Ready. Type a message and press Enter to send.");
}

void loop()
{
    // 1. Check for received RF messages and print them
    uint8_t buf[RH_ASK_MAX_MESSAGE_LEN];
    uint8_t buflen = sizeof(buf);
    if (rf_driver.recv(buf, &buflen)) {
        Serial.print("Received: ");
        for (uint8_t i = 0; i < buflen; i++) {
            Serial.write(buf[i]);
        }
        Serial.println();
    }

    // 2. Check for user input from Serial and send it
    static String inputString = "";
    while (Serial.available() > 0) {
        char inChar = (char)Serial.read();
        if (inChar == '\n' || inChar == '\r') {
            if (inputString.length() > 0) {
                // Send the string as bytes
                rf_driver.send((uint8_t*)inputString.c_str(), inputString.length());
                rf_driver.waitPacketSent();
                Serial.print("Sent: ");
                Serial.println(inputString);
                inputString = "";
            }
        } else {
            inputString += inChar;
        }
    }
}