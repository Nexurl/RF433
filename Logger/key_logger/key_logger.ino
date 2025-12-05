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

// Button pins (4 buttons for menu navigation)
#define BTN_NEXT 12    // Next file
#define BTN_PREV 13    // Previous file
#define BTN_SELECT 14  // Select individual code (optional)
#define BTN_SEND 15    // Send codes from current file

const int chipSelect = CS_PIN;

RCSwitch mySwitch = RCSwitch();
File myFile;
int code_count = 0;

// Menu state
int currentArchiveIndex = 0;  // 0 = location.txt (current), 1+ = archived files
int maxArchiveIndex = 0;      // Will be updated when scanning files (max archive number)
bool menuMode = false;        // Track if we're in menu mode

// Code selection state
bool codeSelectionMode = false;  // True = in code selection mode, False = in file selection mode
int currentCodeIndex = 0;        // Index of currently selected code (0-based)
int maxCodeIndex = 0;            // Total number of codes in current file - 1

void setup() {
  Serial.begin(115200);
  
  // Add delay to let Serial stabilize
  for(int i = 0; i < 10; i++) {
    delay(100);
    Serial.write('.');
  }
  Serial.println("\n\nESP32 Starting...");
  Serial.flush();
  
  // Initialize button pins
  Serial.println("Initializing buttons...");
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_SEND, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  Serial.println("Buttons initialized");
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

  // Disabled: No longer send keys on boot
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
  // Check for button presses
  if (menuMode) {
    handleMenuButtons();
  }
  
  // RF receiver always active
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

void send_keys_from_file(const char* filename) {
  // Disable RF receiver during transmission
  mySwitch.disableReceive();
  Serial.println("RF receiver disabled for transmission");
  delay(100);
  
  File keyFile = SD.open(filename, FILE_READ);
  if (keyFile) {
    // Enable transmitter
    Serial.println("Enabling transmitter for send_keys_from_file");
    pinMode(TX_PIN, OUTPUT);  // Ensure TX pin is in output mode
    digitalWrite(TX_ENABLE_PIN, HIGH);  // Enable the module
    delay(10);
    
    char buffer[33];  // 32 bits + null terminator
    int bufferIndex = 0;
    int codesSent = 0;
    
    Serial.print("Reading and sending codes from ");
    Serial.println(filename);
    
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
  } else {
    Serial.println("Error opening file");
  }
  
  // Disable transmitter
  digitalWrite(TX_ENABLE_PIN, LOW);  // Disable the module
  delay(100);
  
  // Re-enable RF receiver after transmission
  mySwitch.enableReceive(RX_PIN);
  Serial.println("RF receiver re-enabled");
}

// Check if a file is already archived (has a number in the filename)
bool isFileAlreadyArchived(const char* filename) {
  // Check if filename matches pattern "location_N.txt" where N is a number
  String filenameStr = String(filename);
  if (filenameStr.indexOf("location_") >= 0) {
    // Extract the part after "location_"
    int underscorePos = filenameStr.indexOf("location_");
    if (underscorePos >= 0) {
      String afterUnderscore = filenameStr.substring(underscorePos + 9);  // 9 = length of "location_"
      // Check if it starts with a digit (indicating it's archived as location_N.txt)
      if (afterUnderscore.length() > 0 && isdigit(afterUnderscore[0])) {
        return true;
      }
    }
  }
  return false;
}

// Archive the current location.txt file to the next available numbered archive
void archiveFile(const char* filename) {
  // Only archive location.txt, not already-archived files
  if (!String(filename).equals("/keys/location.txt")) {
    Serial.println("ERROR: Can only archive location.txt");
    return;
  }
  
  // Check if the file has at least one code (minimum 25 bytes)
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
  
  // Find the next available archive number
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
  
  // Copy the file content to the archive
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
  
  // Copy all bytes from source to archive
  while (sourceFile.available()) {
    byte buffer[64];
    int bytesRead = sourceFile.read(buffer, 64);
    archiveFileHandle.write(buffer, bytesRead);
  }
  
  archiveFileHandle.flush();
  archiveFileHandle.close();
  sourceFile.close();
  
  // Delete the original file
  if (SD.remove(filename)) {
    Serial.print("File archived as ");
    Serial.println(archiveFilename.c_str());
  } else {
    Serial.println("ERROR: Could not delete original file after archiving");
    return;
  }
  
  // Create a new empty location.txt file
  File newFile = SD.open(filename, FILE_WRITE);
  if (newFile) {
    newFile.close();
    Serial.println("Created new empty location.txt");
  } else {
    Serial.println("ERROR: Could not create new location.txt");
  }
  
  // Rescan for max archive index
  scanMaxArchiveIndex();
}

// Scan SD card for all archived files and find max index
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
  Serial.println("=== MENU ===");
  Serial.print("File: ");
  if (currentArchiveIndex == 0) {
    Serial.println("location.txt (current)");
  } else {
    Serial.print("location_");
    Serial.print(currentArchiveIndex);
    Serial.println(".txt");
  }
  Serial.println("BTN_PREV: Previous | BTN_NEXT: Next | BTN_SEND: Send | BTN_SELECT: Info");
  
  // Display file info
  displayFileInfo();
}

// Display information about the current file
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
      int numCodes = fileSize / 25;  // Each code is 24 bytes + 1 newline
      f.close();
      Serial.print("File: ");
      Serial.print(filename);
      Serial.print(" | Size: ");
      Serial.print(fileSize);
      Serial.print(" bytes | Codes: ");
      Serial.println(numCodes);
      maxCodeIndex = numCodes - 1;  // Update max code index
    }
  }
}

// Get a specific code from the current file by index
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
        // Found the code we want
        strncpy(codeBuffer, buffer, 24);
        codeBuffer[24] = '\0';
        f.close();
        return true;
      }
      codeCount++;
      // Skip the newline
      if (f.available()) {
        f.read();
      }
    }
  }
  
  f.close();
  return false;
}

// Display current code in selection mode
void displayCodeSelectionStatus() {
  char code[25];
  if (getCodeAtIndex(currentCodeIndex, code)) {
    Serial.println("=== CODE SELECTION MODE ===");
    Serial.print("Code ");
    Serial.print(currentCodeIndex + 1);
    Serial.print("/");
    Serial.print(maxCodeIndex + 1);
    Serial.print(": ");
    Serial.println(code);
    Serial.println("BTN_PREV: Previous Code | BTN_NEXT: Next Code | BTN_SEND: Send This Code | BTN_SELECT: Back to File");
  }
}

// Send a single code
void sendSingleCode(const char* code) {
  // Disable RF receiver during transmission
  mySwitch.disableReceive();
  Serial.println("RF receiver disabled for transmission");
  delay(100);
  
  // Enable transmitter
  Serial.println("Enabling transmitter");
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_ENABLE_PIN, HIGH);
  delay(10);
  
  Serial.print("Sending code: ");
  Serial.println(code);
  mySwitch.send(code);
  delay(1000);
  
  // Disable transmitter
  digitalWrite(TX_ENABLE_PIN, LOW);
  delay(100);
  
  Serial.println("Code sent");
  
  // Re-enable RF receiver
  mySwitch.enableReceive(RX_PIN);
  Serial.println("RF receiver re-enabled");
}

// Handle button presses
void handleMenuButtons() {
  // Debounce delay (20ms is good for button debouncing)
  static unsigned long lastButtonTime = 0;
  static unsigned long selectButtonPressTime = 0;
  static bool selectButtonPressed = false;
  unsigned long currentTime = millis();
  
  if (currentTime - lastButtonTime < 50) {
    return;  // Debounce
  }
  
  // Check PREV button
  if (digitalRead(BTN_PREV) == LOW) {
    lastButtonTime = currentTime;
    
    if (codeSelectionMode) {
      // In code selection mode: navigate to previous code
      if (currentCodeIndex > 0) {
        currentCodeIndex--;
        displayCodeSelectionStatus();
      } else if (maxCodeIndex > 0) {
        // Wrap around to last code
        currentCodeIndex = maxCodeIndex;
        displayCodeSelectionStatus();
      }
    } else {
      // In file selection mode: navigate to previous file
      if (currentArchiveIndex > 0) {
        currentArchiveIndex--;
        currentCodeIndex = 0;  // Reset code index when changing files
        printMenuStatus();
      } else if (maxArchiveIndex > 0) {
        // Wrap around to the last archive
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
  
  // Check NEXT button
  if (digitalRead(BTN_NEXT) == LOW) {
    lastButtonTime = currentTime;
    
    if (codeSelectionMode) {
      // In code selection mode: navigate to next code
      if (currentCodeIndex < maxCodeIndex) {
        currentCodeIndex++;
        displayCodeSelectionStatus();
      } else if (maxCodeIndex > 0) {
        // Wrap around to first code
        currentCodeIndex = 0;
        displayCodeSelectionStatus();
      }
    } else {
      // In file selection mode: navigate to next file
      if (currentArchiveIndex < maxArchiveIndex) {
        currentArchiveIndex++;
        currentCodeIndex = 0;
        printMenuStatus();
      } else if (currentArchiveIndex == maxArchiveIndex) {
        // Wrap around to location.txt
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
  
  // Check SEND button
  if (digitalRead(BTN_SEND) == LOW) {
    lastButtonTime = currentTime;
    
    if (codeSelectionMode) {
      // In code selection mode: send the selected code
      char code[25];
      if (getCodeAtIndex(currentCodeIndex, code)) {
        sendSingleCode(code);
        displayCodeSelectionStatus();
      }
    } else {
      // In file selection mode: send all codes from file
      String filename;
      if (currentArchiveIndex == 0) {
        filename = "/keys/location.txt";
      } else {
        filename = String("/keys/location_") + currentArchiveIndex + String(".txt");
      }
      Serial.print("Sending all codes from ");
      Serial.println(filename);
      
      // Check if file exists
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
  
  // Check SELECT button (short press toggles mode, long press archives)
  if (digitalRead(BTN_SELECT) == LOW) {
    if (!selectButtonPressed) {
      // Button just pressed
      selectButtonPressed = true;
      selectButtonPressTime = currentTime;
    } else {
      // Button is still held, check for long press (1000ms = 1 second)
      if (currentTime - selectButtonPressTime >= 1000) {
        lastButtonTime = currentTime;
        
        // Long press: Always archive location.txt, regardless of mode
        Serial.println("Long press detected - archiving location.txt");
        archiveFile("/keys/location.txt");
        // Exit code selection mode if we were in it
        codeSelectionMode = false;
        currentCodeIndex = 0;
        // If we were on location.txt (index 0), stay there
        if (currentArchiveIndex != 0) {
          currentArchiveIndex = 0;
        }
        printMenuStatus();
        
        selectButtonPressed = false;
        delay(500);  // Prevent rapid re-triggers
        return;
      }
    }
  } else {
    // Button released
    if (selectButtonPressed) {
      // It was a short press (less than 1000ms)
      unsigned long pressDuration = currentTime - selectButtonPressTime;
      if (pressDuration < 1000) {
        lastButtonTime = currentTime;
        
        if (codeSelectionMode) {
          // Short press in code selection mode: return to file selection
          codeSelectionMode = false;
          currentCodeIndex = 0;
          printMenuStatus();
        } else {
          // Short press in file selection mode: enter code selection mode
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