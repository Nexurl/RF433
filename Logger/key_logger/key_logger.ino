#include <SPI.h>
#include <SD.h>
#include <RCSwitch.h>

// ESP32 SPI pins
#define MOSI_PIN 23
#define MISO_PIN 19
#define CLK_PIN 18
#define CS_PIN 5

// RCSwitch pins
#define RX_PIN 34
#define TX_PIN 26
#define TX_ENABLE_PIN 25

// Button pins
#define BTN_NEXT 13
#define BTN_PREV 12
#define BTN_SELECT 14
#define BTN_SEND 15

const int chipSelect = CS_PIN;

RCSwitch mySwitch = RCSwitch();
File myFile;
int code_count = 0;

// Menu state
int currentArchiveIndex = 0;
int maxArchiveIndex = 0;
bool menuMode = false;

bool codeSelectionMode = false;
int currentCodeIndex = 0;
int maxCodeIndex = 0;

void setup() {
  Serial.begin(115200);
  
  for(int i = 0; i < 10; i++) {
    delay(100);
    Serial.write('.');
  }
  Serial.println("\n\nESP32 Starting...");
  Serial.flush();
  
  Serial.println("Initializing buttons...");
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_SEND, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  Serial.println("Buttons initialized");
  Serial.flush();
  
  Serial.println("Initializing SPI...");
  SPI.begin(CLK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
  pinMode(chipSelect, OUTPUT);
  digitalWrite(chipSelect, HIGH);
  delay(100);
  Serial.println("SPI initialized");
  Serial.flush();
  
  Serial.println("Waiting before RCSwitch init...");
  delay(500);
  Serial.flush();
  
  Serial.println("Initializing RCSwitch...");
  mySwitch.enableReceive(RX_PIN);
  delay(100);
  mySwitch.enableTransmit(TX_PIN);
  delay(100);
  pinMode(TX_ENABLE_PIN, OUTPUT);
  digitalWrite(TX_ENABLE_PIN, LOW);
  Serial.println("RCSwitch initialized");
  Serial.flush();

  while (!SD_Init()) {
    Serial.println("Trying again in 10 seconds...");
    delay(10000);
  }

  // if (SD.exists("/keys/location.txt")) {
  //   send_keys_from_file(SD.open("/keys/location.txt"));
  //   SD.remove("/keys/location.txt");
  // }

  if (!SD.exists("/keys")) {
    Serial.println("Creating keys directory...");
    SD.mkdir("/keys");
  }

  Serial.println("Creating new location.txt file...");
  File locationFile = SD.open("/keys/location.txt", FILE_WRITE);
  locationFile.close();
  
  //SD_PrintDirectory(SD.open("/"), 3);

  code_count = 0;
  
  // Scan for max archive index
  scanMaxArchiveIndex();
  
  // Enable menu mode
  menuMode = true;
  printMenuStatus();

  Serial.println("Setup complete. Menu mode active.");
}

void loop() {
  if (menuMode) {
    handleMenuButtons();
  }
  
  if (mySwitch.available()) {
    unsigned long decimalValue = mySwitch.getReceivedValue();
    char binaryBuffer[25];  // 24 bits + null terminator
    
    decimalToBinaryString(decimalValue, binaryBuffer);
    
    Serial.print("\nReceived Decimal: ");
    Serial.print(decimalValue);
    Serial.print(" / Binary: ");
    Serial.print(binaryBuffer);
    Serial.print(" / ");
    Serial.print(mySwitch.getReceivedBitlength());
    Serial.print("bit ");
    Serial.print("Protocol: ");
    Serial.println(mySwitch.getReceivedProtocol());

    // Store the code only if it doesn't already exist
    storeUniqueCode("/keys/location.txt", binaryBuffer);

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

// Check if code exists in file
bool isCodeInFile(const char* filename, const char* codeToCheck) {
  File file = SD.open(filename, FILE_READ);
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
  if (!SD.exists("/keys")) {
    Serial.println("Directory /keys does not exist, creating...");
    if (!SD.mkdir("/keys")) {
      Serial.println("ERROR: Failed to create /keys directory");
      return;
    }
  }
  
  File file = SD.open(filename, FILE_APPEND);
  if (file) {
    for(int i = 0; i < 24; i++) {
      file.write(code[i]);
    }
    file.write('\n');
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

// Print directory structure recursively
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

// Send all codes from a file
void send_keys_from_file(const char* filename) {
  mySwitch.disableReceive();
  Serial.println("RF receiver disabled for transmission");
  delay(100);
  
  File keyFile = SD.open(filename, FILE_READ);
  if (keyFile) {
    Serial.println("Enabling transmitter for send_keys_from_file");
    pinMode(TX_PIN, OUTPUT);
    digitalWrite(TX_ENABLE_PIN, HIGH);
    delay(10);
    
    char buffer[33];
    int bufferIndex = 0;
    int codesSent = 0;
    
    Serial.print("Reading and sending codes from ");
    Serial.println(filename);
    
    while (keyFile.available()) {
      char c = keyFile.read();
      
      if (c == '\n' || c == '\r') {
        if (bufferIndex > 0) {
          buffer[bufferIndex] = '\0';
          Serial.print("Sending code #");
          Serial.print(codesSent + 1);
          Serial.print(": ");
          Serial.println(buffer);
          mySwitch.send(buffer);
          codesSent++;
          bufferIndex = 0;
          delay(1000);
        }
      } else if (bufferIndex < 32) {
        buffer[bufferIndex] = c;
        bufferIndex++;
      }
    }
    
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
  } else {
    Serial.println("Error opening file");
  }
  
  digitalWrite(TX_ENABLE_PIN, LOW);
  delay(100);
  
  mySwitch.enableReceive(RX_PIN);
  Serial.println("RF receiver re-enabled");
}

// Check if file is already archived
bool isFileAlreadyArchived(const char* filename) {
  String filenameStr = String(filename);
  if (filenameStr.indexOf("location_") >= 0) {
    int underscorePos = filenameStr.indexOf("location_");
    if (underscorePos >= 0) {
      String afterUnderscore = filenameStr.substring(underscorePos + 9);
      if (afterUnderscore.length() > 0 && isdigit(afterUnderscore[0])) {
        return true;
      }
    }
  }
  return false;
}

// Archive location.txt to numbered file
void archiveFile(const char* filename) {
  if (!String(filename).equals("/keys/location.txt")) {
    Serial.println("ERROR: Can only archive location.txt");
    return;
  }
  
  File checkFile = SD.open(filename, FILE_READ);
  if (!checkFile) {
    Serial.println("ERROR: Could not open file to check size");
    return;
  }
  
  uint32_t fileSize = checkFile.size();
  checkFile.close();
  
  if (fileSize < 25) {
    Serial.println("ERROR: File is empty or has no complete codes. Archiving cancelled.");
    return;
  }
  
  int archiveNum = 1;
  String archiveFilename;
  
  for (int i = 1; i <= 100; i++) {
    archiveFilename = String("/keys/location_") + i + String(".txt");
    if (!SD.exists(archiveFilename.c_str())) {
      archiveNum = i;
      break;
    }
  }
  
  archiveFilename = String("/keys/location_") + archiveNum + String(".txt");
  
  File sourceFile = SD.open(filename, FILE_READ);
  if (!sourceFile) {
    Serial.println("ERROR: Could not open source file for archiving");
    return;
  }
  
  File archiveFileHandle = SD.open(archiveFilename.c_str(), FILE_WRITE);
  if (!archiveFileHandle) {
    Serial.println("ERROR: Could not create archive file");
    sourceFile.close();
    return;
  }
  
  while (sourceFile.available()) {
    byte buffer[64];
    int bytesRead = sourceFile.read(buffer, 64);
    archiveFileHandle.write(buffer, bytesRead);
  }
  
  archiveFileHandle.flush();
  archiveFileHandle.close();
  sourceFile.close();
  
  if (SD.remove(filename)) {
    Serial.print("File archived as ");
    Serial.println(archiveFilename.c_str());
  } else {
    Serial.println("ERROR: Could not delete original file after archiving");
    return;
  }
  
  File newFile = SD.open(filename, FILE_WRITE);
  if (newFile) {
    newFile.close();
    Serial.println("Created new empty location.txt");
  } else {
    Serial.println("ERROR: Could not create new location.txt");
  }
  
  scanMaxArchiveIndex();
}

// Find maximum archive file number
void scanMaxArchiveIndex() {
  maxArchiveIndex = 0;
  for (int i = 1; i <= 100; i++) {
    String filename = String("/keys/location_") + i + String(".txt");
    if (SD.exists(filename.c_str())) {
      maxArchiveIndex = i;
    } else {
      break;  // Stop at first missing file
    }
  }
  Serial.print("Found ");
  Serial.print(maxArchiveIndex);
  Serial.println(" archived file(s)");
}

// Print current menu status
void printMenuStatus() {
  Serial.println("\n=== MENU ===");
  Serial.print("File: ");
  if (currentArchiveIndex == 0) {
    Serial.println("location.txt (current)");
  } else {
    Serial.print("location_");
    Serial.print(currentArchiveIndex);
    Serial.println(".txt");
  }
  // Display file info
  displayFileInfo();

  Serial.println("BTN_SEND: Send all codes | BTN_SELECT: Select Mode / (Hold) Archive | BTN_PREV: Previous | BTN_NEXT: Next");
  
  
}

// Display current file information
void displayFileInfo() {
  String filename;
  
  if (currentArchiveIndex == 0) {
    filename = "/keys/location.txt";
  } else {
    filename = String("/keys/location_") + currentArchiveIndex + String(".txt");
  }
  
  if (SD.exists(filename.c_str())) {
    File f = SD.open(filename.c_str(), FILE_READ);
    if (f) {
      uint32_t fileSize = f.size();
      int numCodes = fileSize / 25;
      f.close();
      //Serial.print("Info: ");
      //Serial.print(filename);
      Serial.print("Size: ");
      Serial.print(fileSize);
      Serial.print(" bytes | Codes: ");
      Serial.println(numCodes);
      maxCodeIndex = numCodes - 1;
    }
  }
}

// Retrieve code at specified index from file
bool getCodeAtIndex(int index, char* codeBuffer) {
  String filename;
  if (currentArchiveIndex == 0) {
    filename = "/keys/location.txt";
  } else {
    filename = String("/keys/location_") + currentArchiveIndex + String(".txt");
  }
  
  File f = SD.open(filename.c_str(), FILE_READ);
  if (!f) {
    return false;
  }
  
  int codeCount = 0;
  char buffer[25];
  buffer[24] = '\0';
  
  while (f.available()) {
    int bytesRead = f.read((uint8_t*)buffer, 24);
    if (bytesRead == 24) {
      if (codeCount == index) {
        strncpy(codeBuffer, buffer, 24);
        codeBuffer[24] = '\0';
        f.close();
        return true;
      }
      codeCount++;
      if (f.available()) {
        f.read();
      }
    }
  }
  
  f.close();
  return false;
}

// Display current code selection
void displayCodeSelectionStatus() {
  char code[25];
  if (getCodeAtIndex(currentCodeIndex, code)) {
    Serial.println("\n=== SINGLE CODE SELECTION MODE ===");
    Serial.print("Code ");
    Serial.print(currentCodeIndex + 1);
    Serial.print("/");
    Serial.print(maxCodeIndex + 1);
    Serial.print(": ");
    Serial.println(code);
    Serial.println("BTN_SEND: Send Code | BTN_SELECT: File Mode | BTN_PREV: Previous Code | BTN_NEXT: Next Code");
  }
}

// Transmit a single code
void sendSingleCode(const char* code) {
  mySwitch.disableReceive();
  Serial.println("RF receiver disabled for transmission");
  delay(100);
  
  Serial.println("Enabling transmitter");
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_ENABLE_PIN, HIGH);
  delay(10);
  
  Serial.print("Sending code: ");
  Serial.println(code);
  mySwitch.send(code);
  delay(1000);
  
  digitalWrite(TX_ENABLE_PIN, LOW);
  delay(100);
  
  Serial.println("Code sent");
  
  mySwitch.enableReceive(RX_PIN);
  Serial.println("RF receiver re-enabled");
}

// Process button input and navigate menus
void handleMenuButtons() {
  static unsigned long lastButtonTime = 0;
  static unsigned long selectButtonPressTime = 0;
  static bool selectButtonPressed = false;
  unsigned long currentTime = millis();
  
  if (currentTime - lastButtonTime < 50) {
    return;
  }
  
  if (digitalRead(BTN_PREV) == LOW) {
    lastButtonTime = currentTime;
    
    if (codeSelectionMode) {
      if (currentCodeIndex > 0) {
        currentCodeIndex--;
        displayCodeSelectionStatus();
      } else if (maxCodeIndex > 0) {
        currentCodeIndex = maxCodeIndex;
        displayCodeSelectionStatus();
      }
    } else {
      if (currentArchiveIndex > 0) {
        currentArchiveIndex--;
        currentCodeIndex = 0;
        printMenuStatus();
      } else if (maxArchiveIndex > 0) {
        currentArchiveIndex = maxArchiveIndex;
        currentCodeIndex = 0;
        printMenuStatus();
      } else {
        Serial.println("No archives available");
      }
    }
    delay(200);
    return;
  }
  
  if (digitalRead(BTN_NEXT) == LOW) {
    lastButtonTime = currentTime;
    
    if (codeSelectionMode) {
      if (currentCodeIndex < maxCodeIndex) {
        currentCodeIndex++;
        displayCodeSelectionStatus();
      } else if (maxCodeIndex > 0) {
        currentCodeIndex = 0;
        displayCodeSelectionStatus();
      }
    } else {
      if (currentArchiveIndex < maxArchiveIndex) {
        currentArchiveIndex++;
        currentCodeIndex = 0;
        printMenuStatus();
      } else if (currentArchiveIndex == maxArchiveIndex) {
        currentArchiveIndex = 0;
        currentCodeIndex = 0;
        printMenuStatus();
      } else {
        Serial.println("Only location.txt available");
      }
    }
    delay(200);
    return;
  }
  
  if (digitalRead(BTN_SEND) == LOW) {
    lastButtonTime = currentTime;
    
    if (codeSelectionMode) {
      char code[25];
      if (getCodeAtIndex(currentCodeIndex, code)) {
        sendSingleCode(code);
        displayCodeSelectionStatus();
      }
    } else {
      String filename;
      if (currentArchiveIndex == 0) {
        filename = "/keys/location.txt";
      } else {
        filename = String("/keys/location_") + currentArchiveIndex + String(".txt");
      }
      Serial.print("Sending all codes from ");
      Serial.println(filename);
      
      if (SD.exists(filename.c_str())) {
        send_keys_from_file(filename.c_str());
        printMenuStatus();
      } else {
        Serial.print("File not found: ");
        Serial.println(filename);
      }
    }
    delay(200);
    return;
  }
  
  if (digitalRead(BTN_SELECT) == LOW) {
    if (!selectButtonPressed) {
      selectButtonPressed = true;
      selectButtonPressTime = currentTime;
    } else {
      if (currentTime - selectButtonPressTime >= 1000) {
        lastButtonTime = currentTime;
        
        Serial.println("\nLong press detected - archiving location.txt");
        archiveFile("/keys/location.txt");
        codeSelectionMode = false;
        currentCodeIndex = 0;
        if (currentArchiveIndex != 0) {
          currentArchiveIndex = 0;
        }
        printMenuStatus();
        
        selectButtonPressed = false;
        delay(500);
        return;
      }
    }
  } else {
    if (selectButtonPressed) {
      unsigned long pressDuration = currentTime - selectButtonPressTime;
      if (pressDuration < 1000) {
        lastButtonTime = currentTime;
        
        if (codeSelectionMode) {
          codeSelectionMode = false;
          currentCodeIndex = 0;
          printMenuStatus();
        } else {
          codeSelectionMode = true;
          currentCodeIndex = 0;
          displayCodeSelectionStatus();
        }
      }
      selectButtonPressed = false;
      delay(200);
    }
  }
}