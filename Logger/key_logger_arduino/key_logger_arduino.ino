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

  if (SD.exists("keys/location.txt")) {
    send_keys_from_file(SD.open("keys/location.txt"));
    SD.remove("keys/location.txt");
  }

  if (!SD.exists("key")) {
    Serial.println("Creating keys directory...");
    SD.mkdir("keys");
  }

  Serial.println("Creating keys/location.txt file...");
  File locationFile = SD.open("keys/location.txt", O_WRITE | O_CREAT);
  locationFile.close();
  
  SD_PrintDirectory(SD.open("/"), 2);

  Serial.println("Setup complete.");
}

void loop() {
  if (mySwitch.available()) {
    unsigned long decimalValue = mySwitch.getReceivedValue();
    char binaryBuffer[25];  // 24 bits + null terminator
    
    decimalToBinaryString(decimalValue, binaryBuffer);
    
    Serial.print("Received Decimal: ");
    Serial.print(decimalValue);
    Serial.print(" / Binary: ");
    Serial.print(binaryBuffer);
    Serial.print(" / ");
    Serial.print(mySwitch.getReceivedBitlength());
    Serial.print("bit ");
    Serial.print("Protocol: ");
    Serial.println(mySwitch.getReceivedProtocol());

    // Store the code (including duplicates)
    storeCode("keys/location.txt", binaryBuffer);

    mySwitch.resetAvailable();
  }
}


// Convert decimal to binary string
void decimalToBinaryString(unsigned long value, char* buffer) {
  buffer[24] = '\0';
  for(int i = 23; i >= 0; i--) {
    buffer[i] = (value & 1) ? '1' : '0';
    value = value >> 1;
  }
}

// Convert binary string to decimal
unsigned long binaryStringToDecimal(const char* binary) {
  unsigned long result = 0;
  while(*binary) {
    result = (result << 1) + (*binary++ - '0');
  }
  return result;
}

// Check if code exists in file
bool isCodeInFile(const char* filename, const char* codeToCheck) {
  File file = SD.open(filename, O_READ);
  if (!file) {
    return false;
  }

  char buffer[25];
  buffer[24] = '\0';
  
  while (file.available()) {
    int bytesRead = file.read((uint8_t*)buffer, 24);
    if (bytesRead == 24) {
      if (strcmp(buffer, codeToCheck) == 0) {
        file.close();
        return true;
      }
      if (file.available()) {
        file.read();
      }
    }
  }
  
  file.close();
  return false;
}

// Store code to file
void storeCode(const char* filename, const char* code) {
  File file = SD.open(filename, O_WRITE | O_APPEND);
  if (file) {
    for(int i = 0; i < 24; i++) {
      file.write(code[i]);
    }
    file.write('\n');
    file.close();
    
    Serial.print("Code stored in ");
    Serial.print(filename);
    Serial.print(": ");
    Serial.println(code);
  } else {
    Serial.println("Error opening file for writing");
  }
}

// Store code only if it doesn't already exist
void storeUniqueCode(const char* filename, const char* code) {
  if (!isCodeInFile(filename, code)) {
    storeCode(filename, code);
  } else {
    Serial.println("Code already exists in file");
  }
}


// Initialize SD card
bool SD_Init() {
  Serial.println("\nInitializing SD card...");

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

// Print directory structure recursively
void SD_PrintDirectory(File dir, int numTabs) {
  while (true) {
    File entry =  dir.openNextFile();

    if (!entry) {
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
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    
    entry.close();
  }

}

// Send all codes from a file
void send_keys_from_file(File keyFile) {
  if (keyFile) {
    char buffer[33];
    int bufferIndex = 0;
    
    Serial.println("Reading and sending codes from exemple.txt:");
    
    while (keyFile.available()) {
      char c = keyFile.read();
      
      if (c == '\n' || c == '\r') {
        if (bufferIndex > 0) {
          buffer[bufferIndex] = '\0';
          Serial.print("Sending code: ");
          Serial.println(buffer);
          mySwitch.send(buffer);
          bufferIndex = 0;
          delay(1000);
        }
      } else if (bufferIndex < 24) {
        buffer[bufferIndex] = c;
        bufferIndex++;
      }
    }
    
    if (bufferIndex > 0) {
      buffer[bufferIndex] = '\0';
      Serial.print("Sending code: ");
      Serial.println(buffer);
      mySwitch.send(buffer);
    }
    
    keyFile.close();
    Serial.println("Finished sending all codes.");
  } else {
    Serial.println("Error opening file");
  }
}