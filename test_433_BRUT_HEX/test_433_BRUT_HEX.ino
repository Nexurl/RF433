// Minimal raw ASK/OOK receiver logger, inspired by RH_ASK::recv
#define RX_PIN 11

void setup()
{
    pinMode(10, OUTPUT); // Enable transmitter
    pinMode(RX_PIN, INPUT); // Data from receiver
    pinMode(12, OUTPUT); // Data to transmitter
    digitalWrite(10, HIGH);  // Transmitter disabled on HIGH
    digitalWrite(12, LOW); // Transmitter data line idle state
    Serial.begin(9600); // Start serial communication
    Serial.println("ça marche");
}

// Simple pulse-to-byte logger: samples RX_PIN, collects 8 bits, prints as hex
void loop()
{
    static uint8_t byteValue = 0;
    static uint8_t bitCount = 0;
    static int lastState = LOW;
    int state = digitalRead(RX_PIN);

    // Detect rising edge (simple pulse detection)
    if (state == HIGH && lastState == LOW) {
        // Shift in a '1' bit
        byteValue = (byteValue << 1) | 1;
        bitCount++;
    } else if (state == LOW && lastState == HIGH) {
        // Shift in a '0' bit (optional: only on falling edge)
        byteValue = (byteValue << 1);
        bitCount++;
    }
    lastState = state;

    // When 8 bits collected, print as hex
    if (bitCount >= 8) {
        Serial.print("0x");
        if (byteValue < 0x10) Serial.print("0");
        Serial.println(byteValue, HEX);
        byteValue = 0;
        bitCount = 0;
    }

    delayMicroseconds(200); // Adjust for your baud rate and signal
}