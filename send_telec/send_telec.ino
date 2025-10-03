#include <RCSwitch.h>

RCSwitch mySwitch = RCSwitch();

void setup() {

 Serial.begin(9600);

 // Transmitter is connected to Arduino Pin #10 
 mySwitch.enableTransmit(10);

 // Optional set pulse length.
 //mySwitch.setPulseLength(321);

 // set protocol (default is 1, will work for most outlets)
 // mySwitch.setProtocol(2);

 // Optional set number of transmission repetitions.
 // mySwitch.setRepeatTransmit(15);

 pinMode(13,OUTPUT);

}

void loop() {
 digitalWrite(LED_BUILTIN,HIGH);
 delay(500);
 mySwitch.send("011011111000010000101000");
 digitalWrite(LED_BUILTIN,LOW);
 delay(3000); 
}