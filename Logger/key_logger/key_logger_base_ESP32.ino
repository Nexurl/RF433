#include <SPI.h>
#include <SD.h>
#include <RCSwitch.h>

// ESP32 SPI pins (adjust if using different SPI bus)
#define MOSI_PIN 23
#define MISO_PIN 19
#define CLK_PIN 18
#define CS_PIN 5

// RCSwitch pins (avoid GPIO 0 and 3 on ESP32 — they conflict with boot/serial)
#define RX_PIN 34      // Receiver input pin
#define TX_PIN 26      // Transmitter data pin
#define TX_ENABLE_PIN 25  // Transmitter enable pin

const int chipSelect = CS_PIN;

RCSwitch mySwitch = RCSwitch();
File myFile;
int code_count = 0;

void setup() {
  Serial.begin(115200);
  
  // Add delay to let Serial stabilize
  for(int i = 0; i < 10; i++) {
    delay(100);
    Serial.write('.');
  }
  Serial.println("\n\nESP32 Starting...");
  Serial.flush();
  
  // Initialize SPI with ESP32 pins
  Serial.println("Initializing SPI...");
  SPI.begin(CLK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
  pinMode(chipSelect, OUTPUT);
  digitalWrite(chipSelect, HIGH);
  delay(100);
  Serial.println("SPI initialized");
  Serial.flush();
  
  // Initialize RCSwitch after SPI and a delay to let SD stabilize
  Serial.println("Waiting before RCSwitch init...");
  delay(500);
  Serial.flush();
  
  Serial.println("Initializing RCSwitch...");
  // Use GPIO 34 (input-only, no SPI conflict) for receiver
  // Use GPIO 26 for transmitter data (also safe from SPI)
  mySwitch.enableReceive(RX_PIN);  // Receiver on GPIO 34
  delay(100);
  mySwitch.enableTransmit(TX_PIN);  // Transmitter data on GPIO 26
  delay(100);
  // Setup transmitter enable pin
  pinMode(TX_ENABLE_PIN, OUTPUT);
  digitalWrite(TX_ENABLE_PIN, LOW);  // Start disabled
  Serial.println("RCSwitch initialized");
  Serial.flush();

  while (!SD_Init()) {
    Serial.println("Trying again in 10 seconds...");
    delay(10000);
  }

  if (SD.exists("/keys/location.txt")) {
    send_keys_from_file(SD.open("/keys/location.txt"));
    SD.remove("/keys/location.txt");
  }

  if (!SD.exists("/keys")) {
    Serial.println("Creating keys directory...");
    SD.mkdir("/keys");
  }

  Serial.println("Creating new location.txt file...");
  File locationFile = SD.open("/keys/location.txt", FILE_WRITE);
  locationFile.close();
  
  //SD_PrintDirectory(SD.open("/"), 3);

  code_count = 0;

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
    storeCode("/keys/location.txt", binaryBuffer);

    mySwitch.resetAvailable();
  }
}


// Convert decimal to binary string (24 bits)
void decimalToBinaryString(unsigned long value, char* buffer) {
  buffer[24] = '\0';  // Null terminate the string
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

bool isCodeInFile(const char* filename, const char* codeToCheck) {
  File file = SD.open(filename, FILE_READ);
  if (!file) {
    return false;  // File doesn't exist, so code can't be in it
  }

  char buffer[25];  // 24 bits + null terminator
  buffer[24] = '\0';  // Always null terminate
  
  while (file.available()) {
    // Read exactly 24 characters
    int bytesRead = file.read((uint8_t*)buffer, 24);
    if (bytesRead == 24) {
      if (strcmp(buffer, codeToCheck) == 0) {
        file.close();
        return true;  // Found a match
      }
      // Skip the newline character
      if (file.available()) {
        file.read();
      }
    }
  }
  
  file.close();
  return false;
}

void storeCode(const char* filename, const char* code) {
  // Ensure /keys directory exists
  if (!SD.exists("/keys")) {
    Serial.println("Directory /keys does not exist, creating...");
    if (!SD.mkdir("/keys")) {
      Serial.println("ERROR: Failed to create /keys directory");
      return;
    }
  }
  
  File file = SD.open(filename, FILE_APPEND);
  if (file) {
    // Write exactly 24 bits
    for(int i = 0; i < 24; i++) {
      file.write(code[i]);
    }
    file.write('\n');  // Single newline character
    
    // Flush to ensure data is written immediately
    file.flush();
    uint32_t fileSize = file.size();
    file.close();

    code_count++;
    
    Serial.print("Code #");
    Serial.print(code_count);
    Serial.print(" stored in ");
    Serial.print(filename);
    Serial.print(" (file size: ");
    Serial.print(fileSize);
    Serial.print(" bytes): ");
    Serial.println(code);
  } else {
    Serial.print("ERROR: Failed to open file for writing: ");
    Serial.println(filename);
  }
}

void storeUniqueCode(const char* filename, const char* code) {
  if (!isCodeInFile(filename, code)) {
    storeCode(filename, code);
  } else {
    Serial.println("Code already exists in file");
  }
}


bool SD_Init() {
  Serial.println("\nInitializing SD card...");

  // ESP32: use full SPI pin specification
  // SD.begin(CS_PIN, SPI, frequency, mount_point, max_open_files)
  if (!SD.begin(CS_PIN, SPI, 25000000, "/sd", 5)) {
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

void send_keys_from_file(File keyFile) {
  if (keyFile) {
    // Enable transmitter
    Serial.println("Enabling transmitter for send_keys_from_file");
    digitalWrite(TX_ENABLE_PIN, HIGH);  // Enable the module
    delay(10);
    
    char buffer[33];  // 32 bits + null terminator
    int bufferIndex = 0;
    int codesSent = 0;
    
    Serial.println("Reading and sending codes from location.txt");
    
    while (keyFile.available()) {
      char c = keyFile.read();
      
      if (c == '\n' || c == '\r') {
        if (bufferIndex > 0) {  // If we have collected some characters
          buffer[bufferIndex] = '\0';  // Null terminate the string
          Serial.print("Sending code #");
          Serial.print(codesSent + 1);
          Serial.print(": ");
          Serial.println(buffer);
          mySwitch.send(buffer);
          codesSent++;
          bufferIndex = 0;  // Reset for next line
          delay(1000);  // Wait 1 second between transmissions
        }
      } else if (bufferIndex < 32) {  // Prevent buffer overflow
        buffer[bufferIndex] = c;
        bufferIndex++;
      }
    }
    
    // Handle the last line if it doesn't end with a newline
    if (bufferIndex > 0) {
      buffer[bufferIndex] = '\0';
      Serial.print("Sending code #");
      Serial.print(codesSent + 1);
      Serial.print(": ");
      Serial.println(buffer);
      mySwitch.send(buffer);
      codesSent++;
    }
    
    keyFile.close();
    Serial.print("Finished sending ");
    Serial.print(codesSent);
    Serial.println(" code(s).");
    // Disable transmitter
    digitalWrite(TX_ENABLE_PIN, LOW);  // Disable the module
    pinMode(TX_PIN, INPUT);
    Serial.println("Transmitter disabled");
  } else {
    Serial.println("Error opening file");
  }
}