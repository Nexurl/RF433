// Custom 433 MHz Protocol - Transmitter
// Based on RadioHead RH_ASK principles but simplified for Arduino UNO
// TX: pin 3, EN_TX: pin 10

#include <Arduino.h>

#define TX_PIN 3
#define EN_TX_PIN 10
#define SPEED 2000  // bits per second

// CRC helpers
#define lo8(x) ((x)&0xff) 
#define hi8(x) ((x)>>8)

// 4-to-6 bit encoding table
// Each symbol has 3 ones and 3 zeros for DC balance
const uint8_t symbols[16] = {
    0x0d, 0x0e, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,
    0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x32, 0x34
};

#define START_SYMBOL 0xb38  // 12-bit start symbol
#define PREAMBLE_LEN 8      // Number of 6-bit preamble symbols
#define MAX_MESSAGE_LEN 60

volatile uint8_t txBuf[256];    // Encoded transmit buffer
volatile uint16_t txBufLen = 0;
volatile uint16_t txIndex = 0;
volatile uint8_t txBit = 0;
volatile uint8_t txSample = 0;
volatile bool transmitting = false;

void setup() {
    Serial.begin(115200);
    pinMode(TX_PIN, OUTPUT);
    pinMode(EN_TX_PIN, OUTPUT);
    digitalWrite(TX_PIN, LOW);
    digitalWrite(EN_TX_PIN, LOW);
    
    setupTimer();
    Serial.println("Custom 433MHz TX ready");
}

void setupTimer() {
    // Timer1 setup for 8 samples per bit at 2000 bps = 16000 Hz
    noInterrupts();
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    
    // 16MHz / (prescaler * (OCR1A + 1)) = 16000 Hz
    // Using prescaler 1: OCR1A = (16000000 / 16000) - 1 = 999
    OCR1A = 999;
    TCCR1B |= (1 << WGM12);  // CTC mode
    TCCR1B |= (1 << CS10);   // Prescaler = 1
    TIMSK1 |= (1 << OCIE1A); // Enable timer compare interrupt
    interrupts();
}

// CRC-CCITT calculation
uint16_t crc_ccitt_update(uint16_t crc, uint8_t data) {
    data ^= crc & 0xff;
    data ^= data << 4;
    return ((((uint16_t)data << 8) | ((crc >> 8) & 0xff)) ^
            (uint8_t)(data >> 4) ^ ((uint16_t)data << 3));
}

uint16_t RHcrc_ccitt_update (uint16_t crc, uint8_t data)
{
    data ^= lo8 (crc);
    data ^= data << 4;
    
    return ((((uint16_t)data << 8) | hi8 (crc)) ^ (uint8_t)(data >> 4) 
	    ^ ((uint16_t)data << 3));
}

// Encode a byte as two 6-bit symbols
void encodeByte(uint8_t b) {
    txBuf[txBufLen++] = symbols[b >> 4];     // High nybble
    txBuf[txBufLen++] = symbols[b & 0x0f];   // Low nybble
}

bool send(const uint8_t* message, uint8_t len) {
    if (transmitting || len > MAX_MESSAGE_LEN) {
        return false;
    }
    
    // Build encoded buffer
    txBufLen = 0;
    
    // Preamble: alternating 0x2a (101010)
    for (uint8_t i = 0; i < PREAMBLE_LEN - 2; i++) {
        txBuf[txBufLen++] = 0x2a;
    }
    
    // Start symbol 0xb38 as two 6-bit symbols (0x38, 0x2c before encoding)
    txBuf[txBufLen++] = 0x38;
    txBuf[txBufLen++] = 0x2c;
    
    // Calculate total length: count(1) + message + FCS(2)
    uint8_t totalLen = len + 3;
    
    // Encode length
    encodeByte(totalLen);
    
    // Calculate CRC
    uint16_t crc = 0xffff;
    crc = RHcrc_ccitt_update(crc, totalLen);
    
    // Encode message
    for (uint8_t i = 0; i < len; i++) {
        encodeByte(message[i]);
        crc = RHcrc_ccitt_update(crc, message[i]);
    }
    
    // Encode CRC (low byte first)
    // encodeByte(crc & 0xff);
    encodeByte(crc >> 8);
    encodeByte(crc & 0xff);
    
    // DEBUG
    Serial.print("CRC :");
    Serial.print(crc, HEX);
    Serial.println();

    // Start transmission
    noInterrupts();
    txIndex = 0;
    txBit = 0;
    txSample = 0;
    transmitting = true;
    digitalWrite(EN_TX_PIN, HIGH);  // Enable transmitter
    interrupts();
    
    // DEBUG
    Serial.print("Entire txBuf :");
    for (uint16_t i = 0; i < txBufLen; i++) {
        Serial.print(txBuf[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    return true;
}

// Timer interrupt - called 8 times per bit
ISR(TIMER1_COMPA_vect) {
    if (!transmitting) return;
    
    if (txSample++ == 0) {
        // Send next bit
        if (txIndex >= txBufLen) {
            // Transmission complete
            digitalWrite(TX_PIN, LOW);
            digitalWrite(EN_TX_PIN, LOW);
            transmitting = false;
        } else {
            // Send bit from current symbol (LSB first)
            digitalWrite(TX_PIN, (txBuf[txIndex] & (1 << txBit)) ? HIGH : LOW);
            txBit++;
            if (txBit >= 6) {
                txBit = 0;
                txIndex++;
            }
        }
    }
    
    if (txSample >= 8) {
        txSample = 0;
    }
}

void loop() {
    static uint32_t lastSend = 0;
    static uint8_t counter = 33;
    
    if (millis() - lastSend >= 1000 && !transmitting) {
        // Send a test message every second
        uint8_t message[10];
        message[0] = 0x42;  // Test pattern
        if (counter > 124) counter = 33;
        message[1] = counter++;
        
        sprintf((char*)&message[2], "TEST%02d", counter);
        
        char displaymessage[8];
        if (send(message, 8)) {
            // DEBUG
            Serial.print("Sending: ");
            for (int i = 0; i < 8; i++) {
                Serial.print(message[i], HEX);
                Serial.print(" ");
                displaymessage[i] = message[i];
            }
            Serial.print(" | ");
            Serial.print(displaymessage);
            Serial.println();
        }
        
        lastSend = millis();
    }
}
