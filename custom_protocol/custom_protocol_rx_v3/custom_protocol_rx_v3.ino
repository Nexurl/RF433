// Custom 433 MHz Protocol - Receiver
// Based on RadioHead RH_ASK principles but simplified for Arduino UNO
// RX: pin 2

#include <Arduino.h>

#define RX_PIN 2
#define TX_PIN 3
#define EN_TX_PIN 10
#define lo8(x) ((x)&0xff) 
#define hi8(x) ((x)>>8)

// 6-to-4 bit decoding table (reverse of encoder)
const uint8_t symbols[16] = {
    0x0d, 0x0e, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,
    0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x32, 0x34
};

#define START_SYMBOL 0xb38  // 12-bit start symbol
#define RX_RAMP_LEN 160     // PLL ramp length
#define RAMP_INC 20         // Standard ramp increment (160/8)
#define RAMP_TRANSITION 80  // Transition point
#define RAMP_ADJUST 9       // Adjustment factor
#define RAMP_INC_RETARD (RAMP_INC - RAMP_ADJUST)
#define RAMP_INC_ADVANCE (RAMP_INC + RAMP_ADJUST)
#define MAX_PAYLOAD_LEN 67

// Receiver state variables
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

uint32_t lastRollingCounter = 0;        // Stocke le dernier compteur accepté
const uint32_t ROLLING_KEY = 0xA5C3F1B7;  // Clé secrète identique au TX
const uint16_t ROLLING_WINDOW = 200;     // Fenêtre d’acceptation

void setup() {
    Serial.begin(115200);
    pinMode(RX_PIN, INPUT);

    // Silence the transmitter
    pinMode(EN_TX_PIN, OUTPUT);
    pinMode(TX_PIN, OUTPUT);
    digitalWrite(EN_TX_PIN, LOW);
    digitalWrite(TX_PIN, LOW);
    
    setupTimer();
    Serial.println("Custom 433MHz RX ready");
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
uint16_t crc_ccitt_update (uint16_t crc, uint8_t data)
{
    data ^= lo8 (crc);
    data ^= data << 4;
    
    return ((((uint16_t)data << 8) | hi8 (crc)) ^ (uint8_t)(data >> 4) 
	    ^ ((uint16_t)data << 3));
}

// Convert 6-bit symbol to 4-bit nybble
uint8_t symbol_6to4(uint8_t symbol) {
    // Linear search through symbol table
    for (uint8_t i = (symbol >> 2) & 8, count = 8; count--; i++) {
        if (symbol == symbols[i]) return i;
    }
    return 0;
}

// Validate received buffer
bool validateRxBuf() {
    if (rxBufLen < 2) return false;
    // Extract checksum (last byte)
    uint8_t received_checksum = rxBuf[rxBufLen - 1];
    // Calculate sum of all previous bytes modulo 256
    uint8_t calc_checksum = 0;
    for (uint8_t i = 0; i < rxBufLen - 1; i++) {
        calc_checksum += rxBuf[i];
    }
    calc_checksum = calc_checksum & 0xFF;
    Serial.print("Received checksum: ");
    Serial.println(received_checksum, HEX);
    Serial.print("Calculated checksum: ");
    Serial.println(calc_checksum, HEX);
    // Valid if calculated checksum matches received checksum
    return (calc_checksum == received_checksum);
}

uint16_t rollingHash(const uint8_t* data, uint8_t len, uint32_t counter) {
    uint32_t hash = counter ^ ROLLING_KEY;

    for (uint8_t i = 0; i < len; i++) {
        hash ^= (uint32_t)data[i] << ((i % 4) * 8);
        hash = (hash << 5) | (hash >> 27); // rotation
        hash *= 0x45d9f3b;
    }
    return (hash ^ (hash >> 16)) & 0xFFFF;
}

bool validateRollingCode(uint8_t* buf, uint8_t len) {
    if (len < 7) return false; // Trop court

    // --------- extraction des champs ----------
    uint8_t dataLen = len - 1;   // -1 pour le checksum classique

    // Structure : [LEN][DATA...][COUNTER32][HASH16][CHECKSUM]
    // On enlève LEN au début et CHECKSUM à la fin :
    uint8_t usefulLen = dataLen - 1 - 4 - 2;

    if (usefulLen <= 0) return false;

    uint8_t* dataPtr = &buf[1];               // Données
    uint8_t* counterPtr = &buf[1 + usefulLen]; // Counter32
    uint8_t* hashPtr = counterPtr + 4;        // Hash16

    // Reconstruction du counter
    uint32_t counter = ((uint32_t)counterPtr[0] << 24) |
                       ((uint32_t)counterPtr[1] << 16) |
                       ((uint32_t)counterPtr[2] << 8)  |
                       ((uint32_t)counterPtr[3]);

    uint16_t receivedHash = ((uint16_t)hashPtr[0] << 8) |
                            ((uint16_t)hashPtr[1]);

    // --------- RÈGLE 1 : Rolling window anti-replay ----------
    if (counter <= lastRollingCounter) {
        Serial.println("ROLL: Counter trop faible → REPLAY ATTACK");
        return false;
    }
    if (counter - lastRollingCounter > ROLLING_WINDOW) {
        Serial.println("ROLL: Counter hors fenêtre");
        return false;
    }

    // --------- RÈGLE 2 : Vérification cryptographique ----------
    uint16_t calcHash = rollingHash(dataPtr, usefulLen, counter);

    Serial.print("ROLL received hash=0x");
    Serial.print(receivedHash, HEX);
    Serial.print(" calculated=0x");
    Serial.println(calcHash, HEX);

    if (calcHash != receivedHash) {
        Serial.println("ROLL: Authentification FAILED");
        return false;
    }

    // OK → mise à jour
    lastRollingCounter = counter;
    Serial.println("ROLL: OK (auth + window)");
    return true;
}

// Timer interrupt - called 8 times per bit
ISR(TIMER1_COMPA_vect) {
    bool rxSample = digitalRead(RX_PIN);
    
    // Integrate samples
    if (rxSample) rxIntegrator++;
    
    // Digital PLL - adjust timing based on transitions
    if (rxSample != rxLastSample) {
        // Transition detected
        rxPllRamp += (rxPllRamp < RAMP_TRANSITION) ? RAMP_INC_RETARD : RAMP_INC_ADVANCE;
        rxLastSample = rxSample;
    } else {
        // No transition - advance normally
        rxPllRamp += RAMP_INC;
    }
    
    // When ramp completes, we have one bit period
    if (rxPllRamp >= RX_RAMP_LEN) {
        // Shift in new bit based on integrator
        rxBits >>= 1;
        if (rxIntegrator >= 5) {  // Threshold: 5 out of 8 samples
            rxBits |= 0x800;
        }
        
        rxPllRamp -= RX_RAMP_LEN;
        rxIntegrator = 0;
        
        if (rxActive) {
            // Collecting message bits
            if (++rxBitCount >= 12) {
                // Have 12 bits = 2 symbols = 1 byte
                uint8_t thisByte = (symbol_6to4(rxBits & 0x3f) << 4) | 
                                   symbol_6to4(rxBits >> 6);
                
                Serial.print("byte : ");
                Serial.println(thisByte, HEX);

                if (rxBufLen == 0) {
                    // First byte is the count
                    rxCount = thisByte;
                    if (rxCount < 3 || rxCount > MAX_PAYLOAD_LEN) {
                        // Invalid count, abort
                        rxActive = false;
                        return;
                    }
                }
                
                rxBuf[rxBufLen++] = thisByte;
                
                if (rxBufLen >= rxCount) {
                    // Message complete
                    rxActive = false;
                    rxBufFull = true;
                }
                rxBitCount = 0;
            }
        } else if (rxBits == START_SYMBOL) {
            // Start symbol detected
            rxActive = true;
            rxBitCount = 0;
            rxBufLen = 0;
        }
    }
}

void loop() {
    // Check for received messages
    if (rxBufFull) {
        noInterrupts();
        uint8_t len = rxBufLen;
        Serial.print("Len = ");
        Serial.println(len);

        uint8_t buf[MAX_PAYLOAD_LEN];
        for (uint8_t i = 0; i < len; i++) {
            buf[i] = rxBuf[i];
        }
        rxBufFull = false;
        interrupts();
        
        // Validate CRC
        if (validateRxBuf()) {
            if (!validateRollingCode(buf, len)) {
                Serial.println("Rolling code invalid");
                return;
            }
            Serial.println("Rolling code valid");

            Serial.print("Received (");
            Serial.print(len);
            Serial.print(" bytes): ");
            
            // Skip length byte, print payload (excluding 2-byte CRC at end)
            for (uint8_t i = 1; i < len; i++) {//- 2; i++) {
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
            Serial.println("CRC Error");
        }
    }
}
