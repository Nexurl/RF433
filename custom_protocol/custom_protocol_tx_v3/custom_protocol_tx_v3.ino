// Custom 433 MHz Protocol - Transmitter
// Based on RadioHead RH_ASK principles but simplified for Arduino UNO
// TX: pin 3, EN_TX: pin 10

#include <Arduino.h>

#define RX_PIN 2
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
volatile uint8_t txData[256];    // Non-Encoded transmit buffer
volatile uint16_t txBufLen = 0;
volatile uint16_t txDataLen = 0;
volatile uint16_t txIndex = 0;
volatile uint8_t txBit = 0;
volatile uint8_t txSample = 0;
volatile bool transmitting = false;

uint32_t rollingCounter = 1;      // Counter évolutif
const uint32_t ROLLING_KEY = 0xA5C3F1B7;   // Clé secrète 32 bits TX/RX

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

// CRC OFFICIEL DE RH_ASK
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

uint16_t rollingHash(const uint8_t* data, uint8_t len, uint32_t counter) {
    uint32_t hash = counter ^ ROLLING_KEY;

    for (uint8_t i = 0; i < len; i++) {
        hash ^= (uint32_t)data[i] << ((i % 4) * 8);
        hash = (hash << 5) | (hash >> 27); // rotation gauche
        hash *= 0x45d9f3b;
    }
    return (hash ^ (hash >> 16)) & 0xFFFF;
}

bool send(const uint8_t* message, uint8_t len) {
    if (transmitting || len > MAX_MESSAGE_LEN) {
        return false;
    }
    
    // Build encoded buffer
    txBufLen = 0;
    txDataLen = 0;

    // Preamble: alternating 0x2a (101010)
    for (uint8_t i = 0; i < PREAMBLE_LEN - 2; i++) {
        txBuf[txBufLen++] = 0x2a;
    }
    
    // Start symbol 0xb38 as two 6-bit symbols (0x38, 0x2c before encoding)
    txBuf[txBufLen++] = 0x38;
    txBuf[txBufLen++] = 0x2c;
    
    // Calculate total length: count(1) + message + rolling count (4) + hash(2) + checksum(1)
    uint8_t totalLen = len + 8;
    // Encode length

    txData[txDataLen++] = totalLen & 0xff;
    // encodeByte(totalLen);

    // Calculate simple checksum (sum modulo 256)
    // uint8_t checksum = totalLen;
    for (uint8_t i = 0; i < len; i++) {
        txData[txDataLen++] = message[i] & 0xff;
        // encodeByte(message[i]);
        // checksum += message[i];
    }
    // checksum = checksum & 0xFF;

    // Ajouter le rollingCounter au buffer

    txData[txDataLen++] = (rollingCounter >> 24) & 0xFF;
    txData[txDataLen++] = (rollingCounter >> 16) & 0xFF;
    txData[txDataLen++] = (rollingCounter >> 8) & 0xFF;
    txData[txDataLen++] = (rollingCounter) & 0xFF;
    
    // encodeByte((rollingCounter >> 24) & 0xFF);
    // encodeByte((rollingCounter >> 16) & 0xFF);
    // encodeByte((rollingCounter >> 8) & 0xFF);
    // encodeByte((rollingCounter) & 0xFF);

    // Calcule du hash sécurisé sur le message UTILE (data)
    uint16_t auth = rollingHash(message, len, rollingCounter);

    // Ajouter le hash

    txData[txDataLen++] = (auth >> 8) & 0xFF;
    txData[txDataLen++] = auth & 0xFF;

    // encodeByte((auth >> 8) & 0xFF);
    // encodeByte(auth & 0xFF);

    // Incrémenter le compteur seulement après un envoi réussi
    rollingCounter++;

    Serial.print("txDataLen : ");
    Serial.println(txDataLen);
    Serial.print("totalLen : ");
    Serial.println(totalLen);

    uint8_t checksum = 0;
    Serial.print("Entire txData :");
    for (uint8_t i = 0; i < txDataLen; i++) {
        Serial.print(txData[i], HEX);
        Serial.print(" ");
        checksum += txData[i];
        encodeByte(txData[i]);
    }
    checksum = checksum & 0xFF;
    Serial.print(checksum, HEX);
    Serial.println();

    // Encode checksum as last byte
    encodeByte(checksum);
    // DEBUG
    Serial.print("Checksum :");
    Serial.print(checksum, HEX);
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

bool validateTxBuf(uint16_t calculatedCRC) {
    if (txBufLen < 3) return false;
    
    // Calculate CRC over entire buffer
    uint16_t Newcrc = 0xffff;
    for (uint8_t i = 0; i < txBufLen; i++) {
        Newcrc = RHcrc_ccitt_update(Newcrc, txBuf[i]);
    }
    Serial.print("Validate TX Buf - Length: ");
    Serial.print(txBufLen);
    Serial.print("\nCalculated CRC: ");
    Serial.print(Newcrc, HEX);
    Serial.print(" Expected CRC: ");
    Serial.println(0xf0b8, HEX);
    
    // Valid CRC should result in 0xf0b8
    return (Newcrc == 0xf0b8);
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
    static uint8_t counter = 33; // Pour afficher des carateres ASCII visibles
    
    if (millis() - lastSend >= 1000 && !transmitting) {
        // Send a test message every second
        uint8_t message[10];
        message[0] = 0x42;  // Test pattern
        if (counter > 124) counter = 33; // 124 pour rester dans les caracteres ASCII visibles
        message[1] = ++counter;
        sprintf((char*)&message[2], "TEST%02d", counter);
        
        char displaymessage[9];
        if (send(message, 8)) {
            // DEBUG
            Serial.print("Sending: ");
            int i = 0;
            for (i = 0; i < 8; i++) {
                Serial.print(message[i], HEX);
                Serial.print(" ");
                displaymessage[i] = message[i];
                // Serial.print(displaymessage[i]);
            }
            displaymessage[i] = '\0';

            Serial.print(" | ");
            Serial.print(displaymessage);
            Serial.println();
        }
        
        lastSend = millis();
    }
}
