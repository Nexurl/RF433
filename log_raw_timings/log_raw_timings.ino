#define RX_PIN 2
#define TX_PIN 12
#define EN_PIN 10

void setup() {
    pinMode(RX_PIN, INPUT);
    pinMode(TX_PIN, OUTPUT);
    pinMode(EN_PIN, OUTPUT);
    digitalWrite(TX_PIN, LOW);
    digitalWrite(EN_PIN, LOW);
    Serial.begin(115200);
    Serial.println("433 MHz pulse logger ready");
}

void loop() {
    static int lastState = LOW;
    static unsigned long lastChange = 0;

    int state = digitalRead(RX_PIN);
    if (state != lastState) {
        unsigned long now = micros();
        unsigned long duration = now - lastChange;
        lastChange = now;

        Serial.print(state == HIGH ? "H: " : "L: ");
        Serial.print(duration);
        Serial.print(" us\t");

        // Optional: print newline after a long LOW (end of transmission)
        if (state == LOW && duration > 5000) Serial.println();

        lastState = state;
    }
}