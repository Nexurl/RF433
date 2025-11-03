// Custom protocol transmitter for 433 MHz modules
// Connect transmitter data pin to Arduino pin 3

#define RX_PIN 2
#define TX_PIN 3
#define EN_PIN 10
const unsigned int BIT_DURATION = 500; // microseconds per bit
const unsigned int PREAMBLE_DURATION = 8000; // microseconds (long HIGH)
const byte START_BYTE = 0xAA;

void setup() {
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);
}

void sendPreamble() {
  digitalWrite(TX_PIN, HIGH);
  delayMicroseconds(PREAMBLE_DURATION);
  digitalWrite(TX_PIN, LOW);
  delayMicroseconds(BIT_DURATION);
}

void sendByte(byte b) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(TX_PIN, (b & 0x01) ? HIGH : LOW);
    delayMicroseconds(BIT_DURATION);
    b >>= 1;
  }
}

void loop() {
  byte payload = 0x42; // Example payload
  byte checksum = payload ^ 0xFF;
  sendPreamble();
  sendByte(START_BYTE);
  sendByte(payload);
  sendByte(checksum);
  digitalWrite(TX_PIN, LOW); // Stop bit
  delay(1000); // Send every second
}