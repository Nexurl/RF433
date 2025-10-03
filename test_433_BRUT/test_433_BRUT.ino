

void setup()
{
    pinMode(10, OUTPUT); // Enable transmitter
    pinMode(11, INPUT); // Data from receiver
    pinMode(12, OUTPUT); // Data to transmitter
    digitalWrite(10, HIGH);  // Transmitter disabled on HIGH
    digitalWrite(12, LOW); // Transmitter data line idle state
    Serial.begin(9600); // Start serial communication
    Serial.println("ça marche");
}

void loop()
{
    int value = digitalRead(11);
    Serial.println(value);
}