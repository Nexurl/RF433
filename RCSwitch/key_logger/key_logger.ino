#include <SPI.h>
#include <SD.h>
#include <RCSwitch.h>

const int chipSelect = 4;

RCSwitch mySwitch = RCSwitch();
File myFile;

void setup() {
  Serial.begin(9600);

  mySwitch.enableReceive(0);  // Receiver on interrupt 0 => that is pin #2
  mySwitch.enableTransmit(3);

  while (!SD_Init()) {
    Serial.println("Trying again in 10 seconds...");
    delay(10000);
  }

  if (!SD.exists("keys")) {
    Serial.println("Creating keys directory...");
    SD.mkdir("keys");
  }

  /*File exempleFile = SD.open("keys/exemple.txt", FILE_WRITE);
  exempleFile.println("011011111000010000101000");
  exempleFile.close();*/

  SD_PrintDirectory(SD.open("/"), 1);

  //mySwitch.send("011011111000010000101000");

  Serial.println("Setup complete.");
}

void loop() {
  if (mySwitch.available()) {
    
    Serial.print("Received ");
    Serial.print( mySwitch.getReceivedValue() );
    Serial.print(" / ");
    Serial.print( mySwitch.getReceivedBitlength() );
    Serial.print("bit ");
    Serial.print("Protocol: ");
    Serial.println( mySwitch.getReceivedProtocol() );

    mySwitch.resetAvailable();
    
  }
}


bool SD_Init() {
  Serial.println("\nInitializing SD card...");

  // we'll use the initialization code from the utility libraries

  // since we're just testing if the card is working!

  if (!SD.begin(chipSelect)) {
    Serial.println();
    Serial.println("Initialization failed. Things to check:");
    Serial.println("* is a card inserted?");
    Serial.println("* is your wiring correct?");
    Serial.println("* did you change the chipSelect pin to match your shield or module?");
    Serial.println();

    return false;

  } else {
    Serial.println();
    Serial.println("Wiring is correct and a card is present.");
    Serial.println();
    return true;

  }
}

void SD_PrintDirectory(File dir, int numTabs) {
  while (true) {
    File entry =  dir.openNextFile();

    if (!entry) {
      // no more files
      break;
    }

    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }

    Serial.print(entry.name());

    if (entry.isDirectory()) {
      Serial.println("/");
      SD_PrintDirectory(entry, numTabs + 1);

    } else {
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    
    entry.close();
  }

}

void keys_from_file(){
  File keyFile = SD.open("keys/exemple.txt");
  if (keyFile) {
    Serial.println("exemple.txt:");
    // read from the file until there's nothing else in it:
    while (keyFile.available()) {
      Serial.write(keyFile.read());
    }
    // close the file:
    keyFile.close();
  } else {
    // if the file didn't open, print an error:
    Serial.println("error opening file");
  }
}