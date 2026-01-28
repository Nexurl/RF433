// Custom 433 MHz Protocol - RX Brute Force ROLLING_KEY
// Attempts to brute-force the ROLLING_KEY by trying all possible values
// Based on custom_protocol_rx_v3.ino, but without a known ROLLING_KEY
// RX: pin 2

#include <Arduino.h>

#define RX_PIN 2
#define TX_PIN 3
#define EN_TX_PIN 10
#define lo8(x) ((x)&0xff) 
#define hi8(x) ((x)>>8)

const uint8_t symbols[16] = {
    0x0d, 0x0e, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,
    0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x32, 0x34
};

#define START_SYMBOL 0xb38
#define RX_RAMP_LEN 160
#define RAMP_INC 20
#define RAMP_TRANSITION 80
#define RAMP_ADJUST 9
#define RAMP_INC_RETARD (RAMP_INC - RAMP_ADJUST)
#define RAMP_INC_ADVANCE (RAMP_INC + RAMP_ADJUST)
#define MAX_PAYLOAD_LEN 67

volatile uint8_t rxIntegrator = 0;
volatile bool rxLastSample = false;
volatile uint8_t rxPllRamp = 0;
volatile uint16_t rxBits = 0;
volatile bool rxActive = false;
volatile uint8_t rxBitCount = 0;
volatile uint8_t rxBuf[MAX_PAYLOAD_LEN];
volatile uint8_t rxBufLen = 0;
volatile uint8_t rxCount = 0;
volatile bool rxBufFull = false;
volatile bool rxBufValid = false;

uint32_t lastRollingCounter = 0;
uint16_t foundKey = 0;
uint32_t foundRollingKey = 0;
const uint16_t ROLLING_WINDOW = 200;

void setup() {
    Serial.begin(115200);
    pinMode(RX_PIN, INPUT);
    pinMode(EN_TX_PIN, OUTPUT);
    pinMode(TX_PIN, OUTPUT);
    digitalWrite(EN_TX_PIN, LOW);
    digitalWrite(TX_PIN, LOW);
    setupTimer();
    Serial.println("Custom 433MHz RX Brute Force ready");
}

void setupTimer() {
    noInterrupts();
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    OCR1A = 999;
    TCCR1B |= (1 << WGM12);
    TCCR1B |= (1 << CS10);
    TIMSK1 |= (1 << OCIE1A);
    interrupts();
}

uint16_t crc_ccitt_update (uint16_t crc, uint8_t data) {
    data ^= lo8 (crc);
    data ^= data << 4;
    return ((((uint16_t)data << 8) | hi8 (crc)) ^ (uint8_t)(data >> 4) 
        ^ ((uint16_t)data << 3));
}

uint8_t symbol_6to4(uint8_t symbol) {
    for (uint8_t i = (symbol >> 2) & 8, count = 8; count--; i++) {
        if (symbol == symbols[i]) return i;
    }
    return 0;
}

bool validateRxBuf() {
    if (rxBufLen < 2) return false;
    uint8_t received_checksum = rxBuf[rxBufLen - 1];
    uint8_t calc_checksum = 0;
    for (uint8_t i = 0; i < rxBufLen - 1; i++) {
        calc_checksum += rxBuf[i];
    }
    calc_checksum = calc_checksum & 0xFF;
    return (calc_checksum == received_checksum);
}

uint16_t rollingHash(const uint8_t* data, uint8_t len, uint32_t counter, uint32_t key) {
    uint32_t hash = counter ^ key;
    for (uint8_t i = 0; i < len; i++) {
        hash ^= (uint32_t)data[i] << ((i % 4) * 8);
        hash = (hash << 5) | (hash >> 27);
        hash *= 0x45d9f3b;
    }
    return (hash ^ (hash >> 16)) & 0xFFFF;
}

bool bruteForceRollingKey(uint8_t* buf, uint8_t len, uint32_t* foundKeyOut, uint32_t* foundCounterOut) {
    if (len < 7) return false;
    uint8_t dataLen = len - 1;
    uint8_t usefulLen = dataLen - 1 - 4 - 2;
    if (usefulLen <= 0) return false;
    uint8_t* dataPtr = &buf[1];
    uint8_t* counterPtr = &buf[1 + usefulLen];
    uint8_t* hashPtr = counterPtr + 4;
    uint32_t counter = ((uint32_t)counterPtr[0] << 24) |
                       ((uint32_t)counterPtr[1] << 16) |
                       ((uint32_t)counterPtr[2] << 8)  |
                       ((uint32_t)counterPtr[3]);
    uint16_t receivedHash = ((uint16_t)hashPtr[0] << 8) |
                            ((uint16_t)hashPtr[1]);
    // Try a limited brute force range for demo (e.g. 0xA5C3F000 to 0xA5C3FFFF)
    for (uint32_t key = 0xA5C3F000; key <= 0xA5C3FFFF; key++) {
        uint16_t calcHash = rollingHash(dataPtr, usefulLen, counter, key);
        if (calcHash == receivedHash) {
            *foundKeyOut = key;
            *foundCounterOut = counter;
            return true;
        }
        if (key % 0x1000 == 0) {
            Serial.print("Checked key: 0x");
            Serial.println(key, HEX);
        }
    }
    return false;
}

ISR(TIMER1_COMPA_vect) {
    bool rxSample = digitalRead(RX_PIN);
    if (rxSample) rxIntegrator++;
    if (rxSample != rxLastSample) {
        rxPllRamp += (rxPllRamp < RAMP_TRANSITION) ? RAMP_INC_RETARD : RAMP_INC_ADVANCE;
        rxLastSample = rxSample;
    } else {
        rxPllRamp += RAMP_INC;
    }
    if (rxPllRamp >= RX_RAMP_LEN) {
        rxBits >>= 1;
        if (rxIntegrator >= 5) {
            rxBits |= 0x800;
        }
        rxPllRamp -= RX_RAMP_LEN;
        rxIntegrator = 0;
        if (rxActive) {
            if (++rxBitCount >= 12) {
                uint8_t thisByte = (symbol_6to4(rxBits & 0x3f) << 4) |
                                   symbol_6to4(rxBits >> 6);
                if (rxBufLen == 0) {
                    rxCount = thisByte;
                    if (rxCount < 3 || rxCount > MAX_PAYLOAD_LEN) {
                        rxActive = false;
                        return;
                    }
                }
                rxBuf[rxBufLen++] = thisByte;
                if (rxBufLen >= rxCount) {
                    rxActive = false;
                    rxBufFull = true;
                }
                rxBitCount = 0;
            }
        } else if (rxBits == START_SYMBOL) {
            rxActive = true;
            rxBitCount = 0;
            rxBufLen = 0;
        }
    }
}

void loop() {
    if (rxBufFull) {
        noInterrupts();
        uint8_t len = rxBufLen;
        uint8_t buf[MAX_PAYLOAD_LEN];
        for (uint8_t i = 0; i < len; i++) {
            buf[i] = rxBuf[i];
        }
        rxBufFull = false;
        interrupts();
        if (validateRxBuf()) {
            uint32_t keyFound = 0;
            uint32_t counterFound = 0;
            if (bruteForceRollingKey(buf, len, &keyFound, &counterFound)) {
                Serial.print("ROLLING_KEY FOUND: 0x");
                Serial.println(keyFound, HEX);
                Serial.print("Counter: ");
                Serial.println(counterFound);
                Serial.print("Received (" );
                Serial.print(len);
                Serial.print(" bytes): ");
                for (uint8_t i = 1; i < len; i++) {
                    if (buf[i] >= 32 && buf[i] < 127) {
                        Serial.print((char)buf[i]);
                    } else {
                        Serial.print("[");
                        Serial.print(buf[i], HEX);
                        Serial.print("]");
                    }
                    Serial.print(" ");
                }
                Serial.println();
            } else {
                Serial.println("ROLLING_KEY not found in range");
            }
        } else {
            Serial.println("CRC Error");
        }
    }
}
