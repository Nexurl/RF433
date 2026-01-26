// Custom 433 MHz Protocol - Bidirectional Rolling Code Transceiver
// Combines RX v3 and TX v3 logic with rolling code authentication
// RX: pin 2, TX: pin 3, EN_TX: pin 10
// Type messages in Serial Monitor to transmit, received messages appear automatically

#include <Arduino.h>

#define RX_PIN 2
#define TX_PIN 3
#define EN_TX_PIN 10
#define SPEED 2000  // bits per second
#define BUTTON_PIN 8

#define lo8(x) ((x)&0xff)
#define hi8(x) ((x)>>8)

const uint8_t symbols[16] = {
    0x0d, 0x0e, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,
    0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x32, 0x34
};

#define START_SYMBOL 0xb38
#define PREAMBLE_LEN 8
#define MAX_MESSAGE_LEN 60
#define MAX_PAYLOAD_LEN 67
#define RX_RAMP_LEN 160
#define RAMP_INC 20
#define RAMP_TRANSITION 80
#define RAMP_ADJUST 9
#define RAMP_INC_RETARD (RAMP_INC - RAMP_ADJUST)
#define RAMP_INC_ADVANCE (RAMP_INC + RAMP_ADJUST)

// Rolling code
uint32_t rollingCounter = 1;
uint32_t lastRollingCounter = 0;
const uint32_t ROLLING_KEY = 0xA5C3F1B7;
const uint16_t ROLLING_WINDOW = 200;

// TX variables
volatile uint8_t txBuf[256];
volatile uint8_t txData[256];
volatile uint16_t txBufLen = 0;
volatile uint16_t txDataLen = 0;
volatile uint16_t txIndex = 0;
volatile uint8_t txBit = 0;
volatile uint8_t txSample = 0;
volatile bool transmitting = false;

// RX variables
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

String inputBuffer = "";

void setup() {
    Serial.begin(115200);
    pinMode(RX_PIN, INPUT);
    pinMode(TX_PIN, OUTPUT);
    pinMode(EN_TX_PIN, OUTPUT);
    digitalWrite(TX_PIN, LOW);
    digitalWrite(EN_TX_PIN, LOW);
    pinMode(BUTTON_PIN, INPUT);
    setupTimer();
    Serial.println("=================================");
    Serial.println("Custom 433MHz Rolling Code Transceiver Ready");
    Serial.println("=================================");
    Serial.println("Type message and press Enter to send");
    Serial.println("Received messages will appear below");
    Serial.println("=================================");
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

uint16_t rollingHash(const uint8_t* data, uint8_t len, uint32_t counter) {
    uint32_t hash = counter ^ ROLLING_KEY;
    for (uint8_t i = 0; i < len; i++) {
        hash ^= (uint32_t)data[i] << ((i % 4) * 8);
        hash = (hash << 5) | (hash >> 27);
        hash *= 0x45d9f3b;
    }
    return (hash ^ (hash >> 16)) & 0xFFFF;
}

void encodeByte(uint8_t b) {
    txBuf[txBufLen++] = symbols[b >> 4];
    txBuf[txBufLen++] = symbols[b & 0x0f];
}

uint8_t symbol_6to4(uint8_t symbol) {
    for (uint8_t i = (symbol >> 2) & 8, count = 8; count--; i++) {
        if (symbol == symbols[i]) return i;
    }
    return 0;
}

bool sendRolling(const uint8_t* message, uint8_t len) {
    if (transmitting || len > MAX_MESSAGE_LEN) return false;
    txBufLen = 0;
    txDataLen = 0;
    for (uint8_t i = 0; i < PREAMBLE_LEN - 2; i++) txBuf[txBufLen++] = 0x2a;
    txBuf[txBufLen++] = 0x38;
    txBuf[txBufLen++] = 0x2c;
    uint8_t totalLen = len + 8;
    txData[txDataLen++] = totalLen & 0xff;
    for (uint8_t i = 0; i < len; i++) txData[txDataLen++] = message[i] & 0xff;
    txData[txDataLen++] = (rollingCounter >> 24) & 0xFF;
    txData[txDataLen++] = (rollingCounter >> 16) & 0xFF;
    txData[txDataLen++] = (rollingCounter >> 8) & 0xFF;
    txData[txDataLen++] = (rollingCounter) & 0xFF;
    uint16_t auth = rollingHash(message, len, rollingCounter);
    txData[txDataLen++] = (auth >> 8) & 0xFF;
    txData[txDataLen++] = auth & 0xFF;
    rollingCounter++;
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < txDataLen; i++) {
        checksum += txData[i];
        encodeByte(txData[i]);
    }
    checksum = checksum & 0xFF;
    encodeByte(checksum);
    noInterrupts();
    txIndex = 0;
    txBit = 0;
    txSample = 0;
    transmitting = true;
    digitalWrite(EN_TX_PIN, HIGH);
    interrupts();
    return true;
}

bool validateRollingCode(uint8_t* buf, uint8_t len) {
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
    if (counter <= lastRollingCounter) return false;
    if (counter - lastRollingCounter > ROLLING_WINDOW) return false;
    uint16_t calcHash = rollingHash(dataPtr, usefulLen, counter);
    if (calcHash != receivedHash) return false;
    lastRollingCounter = counter;
    return true;
}

ISR(TIMER1_COMPA_vect) {
    // TX
    if (transmitting) {
        if (txSample++ == 0) {
            if (txIndex >= txBufLen) {
                digitalWrite(TX_PIN, LOW);
                digitalWrite(EN_TX_PIN, LOW);
                transmitting = false;
            } else {
                digitalWrite(TX_PIN, (txBuf[txIndex] & (1 << txBit)) ? HIGH : LOW);
                txBit++;
                if (txBit >= 6) {
                    txBit = 0;
                    txIndex++;
                }
            }
        }
        if (txSample >= 8) txSample = 0;
    }
    // RX
    if (!transmitting) {
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
            if (rxIntegrator >= 5) rxBits |= 0x800;
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
}

void loop() {
    // Serial input for TX
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() > 0) {
                uint8_t message[MAX_MESSAGE_LEN];
                uint8_t len = min(inputBuffer.length(), (unsigned int)MAX_MESSAGE_LEN);
                for (uint8_t i = 0; i < len; i++) message[i] = inputBuffer[i];
                if (sendRolling(message, len)) {
                    Serial.print("TX: ");
                    Serial.println(inputBuffer);
                } else {
                    Serial.println("ERROR: Could not send (busy or too long)");
                }
                inputBuffer = "";
            }
        } else {
            inputBuffer += c;
        }
    }
    // RX
    if (rxBufFull) {
        noInterrupts();
        uint8_t len = rxBufLen;
        uint8_t buf[MAX_PAYLOAD_LEN];
        for (uint8_t i = 0; i < len; i++) buf[i] = rxBuf[i];
        rxBufFull = false;
        interrupts();
        if (validateRollingCode(buf, len)) {
            Serial.print("RX: ");
            for (uint8_t i = 1; i < len - 7; i++) {
                if (buf[i] >= 32 && buf[i] < 127) Serial.print((char)buf[i]);
                else {
                    Serial.print("[0x");
                    if (buf[i] < 16) Serial.print("0");
                    Serial.print(buf[i], HEX);
                    Serial.print("]");
                }
            }
            Serial.println();
        } else {
            Serial.println("RX: Rolling code invalid or replay detected");
        }
    }
    // Optional: button-based TX
    static uint32_t lastSend = 0;
    static uint8_t counter = 33;
    if (millis() - lastSend >= 1000 && !transmitting && (digitalRead(BUTTON_PIN) == HIGH)) {
        uint8_t message[10];
        message[0] = 0x42;
        if (counter > 124) counter = 33;
        message[1] = ++counter;
        sprintf((char*)&message[2], "TEST%02d", counter);
        sendRolling(message, 8);
        lastSend = millis();
    }
}
