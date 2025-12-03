// Custom 433 MHz Protocol - Bidirectional Transceiver
// Based on RadioHead RH_ASK principles but simplified for Arduino UNO
// RX: pin 2, TX: pin 3, EN_TX: pin 10
// Type messages in Serial Monitor to transmit, received messages appear automatically

#include <Arduino.h>

#define RX_PIN 2
#define TX_PIN 3
#define EN_TX_PIN 10
#define SPEED 2000  // bits per second

// 4-to-6 bit encoding table
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

// TX variables
volatile uint8_t txBuf[256];
volatile uint16_t txBufLen = 0;
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

// Serial input buffer
String inputBuffer = "";

void setup() {
    Serial.begin(115200);
    pinMode(RX_PIN, INPUT);
    pinMode(TX_PIN, OUTPUT);
    pinMode(EN_TX_PIN, OUTPUT);
    digitalWrite(TX_PIN, LOW);
    digitalWrite(EN_TX_PIN, LOW);
    
    setupTimer();
    
    Serial.println("=================================");
    Serial.println("Custom 433MHz Transceiver Ready");
    Serial.println("=================================");
    Serial.println("Type message and press Enter to send");
    Serial.println("Received messages will appear below");
    Serial.println("=================================");
}

void setupTimer() {
    // Timer1 setup for 8 samples per bit at 2000 bps = 16000 Hz
    noInterrupts();
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1 = 0;
    
    OCR1A = 999;  // (16MHz / 16000Hz) - 1
    TCCR1B |= (1 << WGM12);  // CTC mode
    TCCR1B |= (1 << CS10);   // Prescaler = 1
    TIMSK1 |= (1 << OCIE1A); // Enable timer compare interrupt
    interrupts();
}

// CRC-CCITT calculation (for future use)
uint16_t crc_ccitt_update(uint16_t crc, uint8_t data) {
    data ^= crc & 0xff;
    data ^= data << 4;
    return ((((uint16_t)data << 8) | ((crc >> 8) & 0xff)) ^
            (uint8_t)(data >> 4) ^ ((uint16_t)data << 3));
}

// Encode a byte as two 6-bit symbols
void encodeByte(uint8_t b) {
    txBuf[txBufLen++] = symbols[b >> 4];
    txBuf[txBufLen++] = symbols[b & 0x0f];
}

// Convert 6-bit symbol to 4-bit nybble
uint8_t symbol_6to4(uint8_t symbol) {
    for (uint8_t i = (symbol >> 2) & 8, count = 8; count--; i++) {
        if (symbol == symbols[i]) return i;
    }
    return 0;
}

bool send(const uint8_t* message, uint8_t len) {
    if (transmitting || len > MAX_MESSAGE_LEN) {
        return false;
    }
    
    // Build encoded buffer
    txBufLen = 0;
    
    // Preamble
    for (uint8_t i = 0; i < PREAMBLE_LEN - 2; i++) {
        txBuf[txBufLen++] = 0x2a;
    }
    
    // Start symbol
    txBuf[txBufLen++] = 0x38;
    txBuf[txBufLen++] = 0x2c;
    
    // Total length: count + message + FCS
    uint8_t totalLen = len + 3;
    
    // Encode length
    encodeByte(totalLen);
    
    // Calculate CRC
    uint16_t crc = 0xffff;
    crc = crc_ccitt_update(crc, totalLen);
    
    // Encode message
    for (uint8_t i = 0; i < len; i++) {
        encodeByte(message[i]);
        crc = crc_ccitt_update(crc, message[i]);
    }
    
    // Encode CRC
    encodeByte(crc & 0xff);
    encodeByte(crc >> 8);
    
    // Start transmission
    noInterrupts();
    txIndex = 0;
    txBit = 0;
    txSample = 0;
    transmitting = true;
    digitalWrite(EN_TX_PIN, HIGH);
    interrupts();
    
    return true;
}

// Timer interrupt - handles both TX and RX
ISR(TIMER1_COMPA_vect) {
    // TX handling
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
        if (txSample >= 8) {
            txSample = 0;
        }
    }
    
    // RX handling (always active when not transmitting)
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
}

void loop() {
    // Handle Serial input for transmission
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() > 0) {
                // Send the message
                uint8_t message[MAX_MESSAGE_LEN];
                uint8_t len = min(inputBuffer.length(), (unsigned int)MAX_MESSAGE_LEN);
                
                for (uint8_t i = 0; i < len; i++) {
                    message[i] = inputBuffer[i];
                }
                
                if (send(message, len)) {
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
    
    // Handle received messages
    if (rxBufFull) {
        noInterrupts();
        uint8_t len = rxBufLen;
        uint8_t buf[MAX_PAYLOAD_LEN];
        for (uint8_t i = 0; i < len; i++) {
            buf[i] = rxBuf[i];
        }
        rxBufFull = false;
        interrupts();
        
        // Skip CRC check for now - just display the message
        // Format: [length][payload...][crc_low][crc_high]
        Serial.print("RX: ");
        
        // Skip length byte (buf[0]), print payload (excluding 2-byte CRC at end)
        for (uint8_t i = 1; i < len - 2; i++) {
            if (buf[i] >= 32 && buf[i] < 127) {
                Serial.print((char)buf[i]);
            } else {
                Serial.print("[0x");
                if (buf[i] < 16) Serial.print("0");
                Serial.print(buf[i], HEX);
                Serial.print("]");
            }
        }
        Serial.println();
    }
}
