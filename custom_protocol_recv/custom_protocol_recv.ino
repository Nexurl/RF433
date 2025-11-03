/*
  Example for receiving
  
  https://github.com/sui77/rc-switch/
  
  If you want to visualize a telegram copy the raw data and 
  paste it into http://test.sui.li/oszi/
*/


// Custom protocol receiver for 433 MHz modules
// Connect receiver data pin to Arduino pin 2
#include <Arduino.h>
#define RX_PIN 2
#define TX_PIN 3
#define EN_PIN 10

// Protocol parameters
const unsigned int BIT_DURATION = 500; // microseconds per bit
const unsigned int PREAMBLE_DURATION = 8000; // microseconds (long HIGH)
const byte START_BYTE = 0xAA;

void setup() {
  Serial.begin(115200);
  pinMode(RX_PIN, INPUT);
  pinMode(TX_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
  digitalWrite(EN_PIN, LOW);
}

// Wait for a HIGH pulse of at least PREAMBLE_DURATION
bool waitForPreamble() {
  unsigned long t0 = micros();
  while (digitalRead(RX_PIN) == LOW) {
    if (micros() - t0 > 1000000) return false; // Timeout
  }
  t0 = micros();
  while (digitalRead(RX_PIN) == HIGH) {
    if (micros() - t0 > PREAMBLE_DURATION + 1000) break;
  }
  unsigned long pulseLen = micros() - t0;
  return (pulseLen >= PREAMBLE_DURATION);
}

// Read a byte, LSB first
byte readByte() {
  byte value = 0;
  for (int i = 0; i < 8; i++) {
    delayMicroseconds(BIT_DURATION);
    value >>= 1;
    if (digitalRead(RX_PIN) == HIGH) value |= 0x80;
  }
  return value;
}

void loop() {
  if (!waitForPreamble()) return;
  delayMicroseconds(BIT_DURATION); // Wait for start bit
  byte start = readByte();
  if (start != START_BYTE) return;
  byte payload = readByte();
  byte checksum = readByte();
  // Simple checksum: payload ^ 0xFF
  if (checksum == (payload ^ 0xFF)) {
    Serial.print("Received payload: ");
    Serial.println(payload, HEX);
  } else {
    Serial.println("Checksum error");
  }
  // Wait for stop bit (LOW)
  delayMicroseconds(BIT_DURATION);
}