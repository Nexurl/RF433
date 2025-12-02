
// Custom 433 MHz Protocol - Transmitter v3 (Rolling Code)
// TX: pin 3, EN_TX: pin 10
#include <Arduino.h>

#define TX_PIN 3
#define EN_TX_PIN 10
#define SPEED 2000
#define PREAMBLE_LEN 8
#define MAX_MESSAGE_LEN 60

const uint8_t symbols[16] = {
    0x0d, 0x0e, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,
    0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x32, 0x34
};

#define START_SYMBOL 0xb38

volatile uint8_t txBuf[256];
volatile uint16_t txBufLen = 0;
volatile uint16_t txIndex = 0;
volatile uint8_t txBit = 0;
volatile uint8_t txSample = 0;
volatile bool transmitting = false;

uint32_t rolling_code = 0x12345678;

void setup() {
    Serial.begin(115200);
    pinMode(TX_PIN, OUTPUT);
    pinMode(EN_TX_PIN, OUTPUT);
    digitalWrite(TX_PIN, LOW);
    digitalWrite(EN_TX_PIN, LOW);
    Serial.println("Custom 433MHz TX v3 ready");
}

void encodeByte(uint8_t b) {
    txBuf[txBufLen++] = symbols[b >> 4];
    txBuf[txBufLen++] = symbols[b & 0x0f];
}

bool send(const uint8_t* message, uint8_t len) {
    if (transmitting || len > MAX_MESSAGE_LEN) return false;
    txBufLen = 0;
    // Preamble
    for (uint8_t i = 0; i < PREAMBLE_LEN - 2; i++) txBuf[txBufLen++] = 0x2a;
    // Start symbol
    txBuf[txBufLen++] = 0x38;
    txBuf[txBufLen++] = 0x2c;
    // Length byte
    uint8_t totalLen = len + 6; // length + message + rolling_code(4) + checksum(1)
    encodeByte(totalLen);
    // Rolling code (4 bytes, MSB first)
    for (int i = 3; i >= 0; i--) encodeByte((rolling_code >> (8*i)) & 0xFF);
    // Message
    uint8_t checksum = totalLen;
    for (uint8_t i = 0; i < len; i++) {
        encodeByte(message[i]);
        checksum += message[i];
    }
    // Add rolling code bytes to checksum
    for (int i = 3; i >= 0; i--) checksum += (rolling_code >> (8*i)) & 0xFF;
    checksum = checksum & 0xFF;
    encodeByte(checksum);
    // Transmission logic omitted for brevity
    Serial.print("TX rolling code: ");
    Serial.println(rolling_code, HEX);
    Serial.print("TX checksum: ");
    Serial.println(checksum, HEX);
    return true;
}

void loop() {
    static uint32_t lastSend = 0;
    static uint8_t counter = 33;
    if (millis() - lastSend >= 1000 && !transmitting) {
        uint8_t message[8];
        message[0] = 0x42;
        message[1] = counter++;
        sprintf((char*)&message[2], "RC%02d", counter);
        send(message, 8);
        rolling_code++;
        lastSend = millis();
    }
}
