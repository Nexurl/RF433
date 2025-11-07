/*
  Robust custom protocol receiver for 433 MHz OOK modules
  - Pulse-width coding (0 => H 1T / L 2T, 1 => H 2T / L 1T)
  - Preamble (1T/1T toggles), Sync (3T/3T)
  - Framing: [LEN][SEQ][PAYLOAD...][CRC8]
  - Repeats: sender sends each frame 3x; we de-duplicate by SEQ
*/

#include <Arduino.h>

// Connect receiver data pin to Arduino pin 2
#define RX_PIN 2
#define TX_PIN 3
#define EN_PIN 10

// Protocol parameters (must match transmitter)
static const unsigned int T_US = 500;         // base time unit
static const uint8_t PREAMBLE_MIN_CYCLES = 12; // how many 1T/1T pairs to accept preamble
static const float TOL = 0.40f;               // +/- tolerance on pulse width matching (40%)

// CRC8 (poly 0x07)
static uint8_t crc8(const uint8_t* data, uint8_t len) {
  uint8_t crc = 0x00;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x80) crc = (crc << 1) ^ 0x07; else crc <<= 1;
    }
  }
  return crc;
}

static inline bool inRange(unsigned long v, unsigned long target) {
  Serial.print("v = ");
  Serial.println(v);
  // Serial.print("target = ");
  // Serial.println(target);
  unsigned long low = (unsigned long)(target * (1.0f - TOL));
  // Serial.print("low = ");
  // Serial.println(low);
  unsigned long high = (unsigned long)(target * (1.0f + TOL));
  // Serial.print("high = ");
  // Serial.println(high);
    Serial.print("Is in range ? --> ");
    Serial.println(v >= low && v <= high);
  return v >= low && v <= high;
}

// Read one HIGH and one LOW pulse duration (microseconds). Returns false on timeout.
static bool readPulsePair(unsigned long timeout_us, unsigned long &highDur, unsigned long &lowDur) {
  // Measure one HIGH pulse then one LOW pulse
  highDur = pulseIn(RX_PIN, HIGH, timeout_us);
  Serial.print("highDur = ");
  Serial.print(highDur);
  lowDur = pulseIn(RX_PIN, LOW, timeout_us);
  Serial.print("   /   ");
  Serial.print("lowDur = ");
  Serial.println(highDur);
  if (highDur == 0) return false;
  if (lowDur == 0) return false;
  return true;
}

// Wait for preamble (a run of ~1T/1T pairs) followed by sync (3T/3T)
static bool waitForPreambleAndSync() {
  unsigned long highDur = 0, lowDur = 0;
  uint8_t cycles = 1;
  unsigned long timeout = 100000; // 100ms overall attempt
  unsigned long start = micros();

  // First, accumulate at least PREAMBLE_MIN_CYCLES of 1T/1T
  while ((micros() - start) < timeout && cycles < PREAMBLE_MIN_CYCLES) {
    Serial.print("Diff de timing ");
    Serial.print(micros() - start);
    Serial.print(" Nombre de cycles : ");
    Serial.println(cycles);
    if (!readPulsePair(6000, highDur, lowDur)) continue;
    if (inRange(highDur, T_US) && inRange(lowDur, T_US)) {
      Serial.print("cycle = ");
      Serial.println(cycles);
      cycles++;
    } else {
      Serial.println("RESET CYCLES");
      cycles = 0; // reset if broken
    }
  }
  if (cycles < PREAMBLE_MIN_CYCLES) return false;

  // Now expect a sync of ~3T/3T
  if (!readPulsePair(10000, highDur, lowDur)) return false;
  if (!(inRange(highDur, 3 * T_US) && inRange(lowDur, 3 * T_US))) return false;
  return true;
}

// Read a single data bit using pulse-width coding
// Returns: -1 on error, 0 or 1 on success
static int8_t readBit() {
  unsigned long highDur = 0, lowDur = 0;
  if (!readPulsePair(6000, highDur, lowDur)) return -1;
  bool high1T = inRange(highDur, 1 * T_US);
  bool high2T = inRange(highDur, 2 * T_US);
  bool low1T  = inRange(lowDur, 1 * T_US);
  bool low2T  = inRange(lowDur, 2 * T_US);

  if (high2T && low1T) return 1;
  if (high1T && low2T) return 0;
  return -1;
}

static bool readByte(uint8_t &out) {
  uint8_t v = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    int8_t bit = readBit();
    if (bit < 0) return false;
    v = (uint8_t)((v << 1) | (uint8_t)bit); // MSB first
  }
  out = v;
  return true;
}

static bool readPacket(uint8_t &seq, uint8_t *payload, uint8_t &payloadLen) {
  uint8_t len = 0;
  if (!readByte(len)) return false;
  if (len == 0 || len > 17) return false; // sanity (SEQ + up to 16 bytes)

  // Read SEQ + PAYLOAD
  uint8_t temp[1 + 16];
  for (uint8_t i = 0; i < len; ++i) {
    if (!readByte(temp[i])) return false;
  }

  // CRC
  uint8_t rxCrc = 0;
  if (!readByte(rxCrc)) return false;

  // Verify CRC across LEN + (SEQ+PAYLOAD)
  uint8_t crcBuf[1 + 1 + 16];
  crcBuf[0] = len;
  for (uint8_t i = 0; i < len; ++i) crcBuf[1 + i] = temp[i];
  uint8_t calc = crc8(crcBuf, (uint8_t)(1 + len));
  if (calc != rxCrc) return false;

  seq = temp[0];
  payloadLen = (uint8_t)(len - 1);
  for (uint8_t i = 0; i < payloadLen; ++i) payload[i] = temp[1 + i];
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(RX_PIN, INPUT);
  pinMode(TX_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
  digitalWrite(EN_PIN, LOW);
  Serial.print("J'écoute !");
}

void loop() {
  static uint8_t lastSeq = 255;
  static unsigned long lastPrintMs = 0;

  if (!waitForPreambleAndSync()) return;

  uint8_t seq = 0;
  uint8_t payload[16];
  uint8_t payloadLen = 0;
  if (!readPacket(seq, payload, payloadLen)) return;

  // De-duplicate retransmissions by sequence within a short window
  unsigned long nowMs = millis();
  bool isDup = (seq == lastSeq) && (nowMs - lastPrintMs < 300);
  if (isDup) return;

  lastSeq = seq;
  lastPrintMs = nowMs;

  // Print payload bytes
  Serial.print("Received len=");
  Serial.print(payloadLen);
  Serial.print(" seq=");
  Serial.print(seq);
  Serial.print(" payload=");
  for (uint8_t i = 0; i < payloadLen; ++i) {
    Serial.print("0x");
    if (payload[i] < 16) Serial.print('0');
    Serial.print(payload[i], HEX);
    if (i + 1 < payloadLen) Serial.print(' ');
  }
  Serial.println();
}