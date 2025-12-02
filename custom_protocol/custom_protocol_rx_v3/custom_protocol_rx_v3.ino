
// Custom 433 MHz Protocol - Receiver v3 (Rolling Code)
// RX: pin 2
#include <Arduino.h>

#define RX_PIN 2
#define MAX_PAYLOAD_LEN 67

const uint8_t symbols[16] = {
    0x0d, 0x0e, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,
    0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x32, 0x34
};

#define START_SYMBOL 0xb38

volatile uint8_t rxBuf[MAX_PAYLOAD_LEN];
volatile uint8_t rxBufLen = 0;
uint32_t last_rolling_code = 0;

void setup() {
    Serial.begin(115200);
    pinMode(RX_PIN, INPUT);
    Serial.println("Custom 433MHz RX v3 ready");
}

uint8_t symbol_6to4(uint8_t symbol) {
    for (uint8_t i = 0; i < 16; i++) {
        if (symbol == symbols[i]) return i;
    }
    return 0;
}

bool validateRxBuf() {
    if (rxBufLen < 6) return false;
    uint8_t received_checksum = rxBuf[rxBufLen - 1];
    uint8_t calc_checksum = 0;
    for (uint8_t i = 0; i < rxBufLen - 1; i++) calc_checksum += rxBuf[i];
    calc_checksum = calc_checksum & 0xFF;
    // Extract rolling code (bytes 1-4 after length)
    uint32_t received_rolling_code = (rxBuf[1] << 24) | (rxBuf[2] << 16) | (rxBuf[3] << 8) | rxBuf[4];
    // Check rolling code
    if (received_rolling_code >= last_rolling_code) {
        Serial.println("Rolling code not incremented!");
        return false;
    }
    last_rolling_code = received_rolling_code;
    Serial.print("RX rolling code: ");
    Serial.println(last_rolling_code, HEX);
    Serial.print("RX checksum: ");
    Serial.println(calc_checksum, HEX);
    return (calc_checksum == received_checksum);
}

void loop() {
    // Example: after receiving a message and filling rxBuf/rxBufLen
    if (validateRxBuf()) {
        Serial.print("Valid message with rolling code: ");
        Serial.println(last_rolling_code, HEX);
    }
}
