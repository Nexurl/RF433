// Custom protocol transmitter for 433 MHz modules
// Robust OOK pulse-width protocol with preamble, sync, CRC8 and repeats
// Connect transmitter data pin to Arduino pin 3

#include <Arduino.h>

#define RX_PIN 2
#define TX_PIN 3
#define EN_PIN 10

// Base timing unit (T). Bits are encoded as:
//  - '0' => HIGH 1T, LOW 2T
//  - '1' => HIGH 2T, LOW 1T
// Sync => HIGH 3T, LOW 3T
// Preamble => repeat (HIGH 1T, LOW 1T) PREAMBLE_CYCLES times
static const unsigned int T_US = 500;         // microseconds; lower rate = more robust
static const uint8_t PREAMBLE_CYCLES = 24;    // number of 1T/1T toggles before sync
static const uint8_t REPEAT_COUNT = 3;        // transmit each packet N times
static const unsigned int REPEAT_GAP_US = 8000; // gap between repeats (us)

// Packet: [LEN][SEQ][PAYLOAD...][CRC8]
//  - LEN includes SEQ + PAYLOAD bytes (excludes LEN itself and CRC)

static inline void txHighLow(unsigned int high_us, unsigned int low_us) {
  digitalWrite(TX_PIN, HIGH);
  delayMicroseconds(high_us);
  digitalWrite(TX_PIN, LOW);
  delayMicroseconds(low_us);
}

static void sendPreambleAndSync() {
  // Optional: enable TX if using a power/enable pin
  // digitalWrite(EN_PIN, HIGH);
  // Preamble: alternating 1T/1T to let AGC settle
  for (uint8_t i = 0; i < PREAMBLE_CYCLES; i++) {
    txHighLow(T_US, T_US);
  }
  // Sync word: 3T high, 3T low
  txHighLow(3 * T_US, 3 * T_US);
}

static inline void sendBit(uint8_t b) {
  if (b) {
    txHighLow(2 * T_US, 1 * T_US);
  } else {
    txHighLow(1 * T_US, 2 * T_US);
  }
}

static void sendByte(uint8_t byteVal) {
  // MSB first for readability; receiver matches this
  for (int8_t i = 7; i >= 0; --i) {
    sendBit((byteVal >> i) & 0x01);
  }
}

static uint8_t crc8(const uint8_t* data, uint8_t len) {
  // CRC-8-ATM/CRC-8-CCITT polynomial 0x07, init 0x00
  uint8_t crc = 0x00;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x80) crc = (crc << 1) ^ 0x07; else crc <<= 1;
    }
  }
  return crc;
}

static void sendPacket(uint8_t seq, const uint8_t* payload, uint8_t payloadLen) {
  uint8_t len = 1 + payloadLen; // SEQ + payload
  uint8_t headerAndPayload[1 /*LEN*/ + 1 /*SEQ*/ + 16 /*max payload*/];
  // Assemble into a linear buffer for CRC calc (LEN + SEQ + PAYLOAD)
  headerAndPayload[0] = len;
  headerAndPayload[1] = seq;
  for (uint8_t i = 0; i < payloadLen; ++i) headerAndPayload[2 + i] = payload[i];
  uint8_t crc = crc8(headerAndPayload, (uint8_t)(2 + payloadLen));

  // Transmit frame: preamble+sync, bytes MSB-first, then CRC
  sendPreambleAndSync();
  sendByte(len);
  sendByte(seq);
  for (uint8_t i = 0; i < payloadLen; ++i) sendByte(payload[i]);
  sendByte(crc);
}

void setup() {
  Serial.begin(115200);
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);
  Serial.println("Je parle !");
}

void loop() {
  static uint8_t seq = 0;
  const uint8_t payload = 0x42; // Example payload

  for (uint8_t i = 0; i < REPEAT_COUNT; ++i) {
    sendPacket(seq, &payload, 1);
    delayMicroseconds(REPEAT_GAP_US);
  }

  seq++;
  // Ensure TX line is idle LOW
  digitalWrite(TX_PIN, LOW);
  delay(1000); // Send every second
}