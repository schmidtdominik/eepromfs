#include <Arduino.h>
#include <EEPROM.h>

#define PRINTBIN(Num) for (uint32_t t = (1UL<< (sizeof(Num)*8)-1); t; t >>= 1) Serial.write(Num  & t ? '1' : '0'); // Prints a binary number with leading zeros (Automatic Handling)

struct File {
  uint16_t address;
  String name;
  bool isDir;
  byte dataSize;
  uint16_t dataStartAddr;
};

// writebuffer is disabled (replace rd/wr ops below)
const int8_t BUFFER_SIZE = 30;
uint16_t writeBufferAddr[BUFFER_SIZE];
uint16_t writeBufferValue[BUFFER_SIZE];
uint8_t writeBufferPointer = 0;
uint16_t totalWriteCycles = 0;

String command[5];
String commandString;

uint16_t fs_size;
struct File cwd[32];
byte cwdPointer = 0;

const uint8_t HEADER_SIZE = 5;

uint8_t allocMap[128];

/*
void flushBuffer() {
  for (uint8_t i = 0; i < writeBufferPointer; i++) {
    EEPROM.write(writeBufferAddr[i], writeBufferValue[i]);
    totalWriteCycles++;
  }
  writeBufferPointer = 0;
}

void writeROM(uint16_t address, uint8_t value) {
  if (EEPROM.read(address) == value) {
    return;
  }

  for (uint8_t i = 0; i < writeBufferPointer; i++) {
    if (writeBufferAddr[i] == address) {
      writeBufferValue[i] = value;
      return;
    }
  }

  writeBufferAddr[writeBufferPointer] = address;
  writeBufferValue[writeBufferPointer] = value;
  writeBufferPointer++;

  if (writeBufferPointer == BUFFER_SIZE) {
    flushBuffer();
  }
}

uint8_t readROM(uint16_t address) {
  for (uint8_t i = 0; i < writeBufferPointer; i++) {
    if (writeBufferAddr[i] == address) {
      return writeBufferValue[i];
    }
  }

  return EEPROM.read(address);
}*/

void writeROM(uint16_t address, uint8_t value) {
  EEPROM.update(address, value);
}

uint8_t readROM(uint16_t address) {
  return EEPROM.read(address);
}

uint16_t readTwoBytes(uint16_t addr) {
  return readROM(addr)*pow(2, 8)+readROM(addr+1);
}

void writeTwoBytes(uint16_t addr, uint16_t value) {
  writeROM(addr, highByte(value));
  writeROM(addr+1, lowByte(value));
}

void wipe() {
  for (uint16_t i = 0; i < EEPROM.length(); i++) {
    writeROM(i, 0);
  }
}

void printIndent(uint8_t indentLevel) {
  for (uint8_t i = 0; i < indentLevel; i++) {
    Serial.print(' ');
  }
}

void printFile(struct File f) {
  Serial.print(F("address: ")); Serial.println(f.address);
  Serial.print(F("name: ")); Serial.println(f.name);
  Serial.print(F("isDir: ")); Serial.println(f.isDir);
  if (f.isDir) {
    Serial.print(F("subfiles: ")); Serial.println(f.dataSize/2);
  } else {
    Serial.print(F("dataSize: ")); Serial.println(f.dataSize);
  }
  Serial.print(F("dataStartAddr: ")); Serial.println(f.dataStartAddr);
}

/* Dump the entire memory content to Serial */
void memdump(bool direct) {
  for (uint16_t addr = 0; addr < EEPROM.length(); addr++) {
    if (addr % 32 == 0) {
      Serial.println();
    }
    byte value;
    if (direct) {
      value = EEPROM.read(addr);
    } else {
      value = readROM(addr);
    }
    Serial.print(value, HEX); Serial.print(' ');
    if (value <= 0xF) {
      Serial.print(' ');
    }
  }
  Serial.println();
}

/* Read and return file starting at addr */
struct File readFile(uint16_t addr) {
  struct File file;
  file.address = addr;

  uint8_t header = readROM(addr);
  file.isDir = bitRead(header, 0);

  uint8_t nameSize = readROM(addr+1);
  uint8_t dataSize = readROM(addr+2);
  file.dataSize = dataSize;

  String name;
  for (uint8_t i = 0; i < nameSize; i++) {
    name += char(readROM(addr+3+i));
  }
  file.name = name;

  file.dataStartAddr = addr+3+nameSize;
  return file;
}

/* Read all subdirectories and files in a dir */
void getSubfiles(struct File f, struct File *result) {
  if (!f.isDir) {
    Serial.println(F("Error: Not a directory"));
  }

  uint8_t j = 0;
  for (uint16_t i = f.dataStartAddr; i < f.dataStartAddr+f.dataSize; i+=2) {
    result[j] = readFile(readTwoBytes(i));
    j++;
  }
}

/* Print current working directory */
void printCwd() {
  for (uint8_t i = 0; i <= cwdPointer; i++) {
    Serial.print(cwd[i].name); Serial.print('/');
  }
}

/* List all and dirs in cwd */
void ls() {
  struct File currentCwd = cwd[cwdPointer];
  struct File subfiles[currentCwd.dataSize/2];
  getSubfiles(currentCwd, subfiles);
  Serial.print(F("Content of ")); printCwd(); Serial.println();
  for (uint8_t i = 0; i < sizeof(subfiles)/sizeof(struct File); i++) {
    Serial.print(subfiles[i].isDir);
    Serial.print('\t');
    Serial.print(subfiles[i].address);
    Serial.print('\t');
    Serial.print(subfiles[i].dataSize);
    Serial.print('\t');
    Serial.println(subfiles[i].name);
  }
}

struct File getFileByName(String name) {
  struct File currentCwd = cwd[cwdPointer];
  struct File subfiles[currentCwd.dataSize/2];
  getSubfiles(currentCwd, subfiles);

  struct File f;
  bool foundDir = false;
  for (uint8_t i = 0; i < sizeof(subfiles)/sizeof(struct File); i++) {
    if (subfiles[i].name == name) {
      f = subfiles[i];
      foundDir = true;
      break;
    }
  }
  if (!foundDir) {
    f.name = F("ERR_FILE_NOT_FOUND");
  }
  return f;
}

/* Move up one level in the file hierarchy ("cd ..") */
void cdPop() {
  if (cwdPointer > 0) {
    cwdPointer--;
  }
}

/* Move into the given directory */
bool cd(String dir) {
  if (dir == "..") {
    cdPop();
    return true;
  }

  // Reset cwd to root dir
  if (dir == "") {
    cwdPointer = 0;
    return true;
  }

  struct File cdInto = getFileByName(dir);

  if (cdInto.name == F("ERR_FILE_NOT_FOUND")) {
    Serial.println(F("Error: Directory not found."));
    return false;
  }

  if (!cdInto.isDir) {
    Serial.println(F("Error: Not a directory."));
    return false;
  }

  cwdPointer++;
  cwd[cwdPointer] = cdInto;
  return true;
}


/* Set memory address alloc state, optionally wipe address on dealloc */
void setAllocMapPos(uint16_t addr, bool value, bool wipeOnDealloc) {
  bitWrite(allocMap[(int) addr/8], 7-addr%8, value);
  if (wipeOnDealloc && !value) {
    writeROM(addr, 0);
  }
}

/* Read current alloc state of memory address */
bool getAllocMapPos(uint16_t addr) {
  return bitRead(allocMap[(int) addr/8], 7-addr%8);
}

/* Dump allocation map to serial */
void dumpAllocMap() {
  for (uint16_t i = 0; i < sizeof(allocMap)/sizeof(byte); i++) {
    if (i % 4 == 0) {
      Serial.println();
    }
    PRINTBIN(allocMap[i]); Serial.print(' ');
    //Serial.print(allocMap[i], BIN); Serial.print(' ');
  }
  Serial.println();
}

/* Recursively set alloc state for file(s) */
void markInAllocMap(struct File f, bool value, bool wipeOnDealloc) {
  if (f.isDir) {
    // Recursively set all subfiles in alloc map
    struct File subfiles[f.dataSize/2];
    getSubfiles(f, subfiles);

    for (uint8_t i = 0; i < sizeof(subfiles)/sizeof(struct File); i++) {
      markInAllocMap(subfiles[i], value, wipeOnDealloc);
    }
  }

  // Set this file in allocation map
  for (uint16_t i = f.address; i < f.dataStartAddr+f.dataSize; i++) {
    setAllocMapPos(i, value, wipeOnDealloc);
  }
}

/* Recreate alloc map from filesystem */
void createAllocMap() {

  // reset alloc map
  for (uint16_t i = 0; i < EEPROM.length(); i++) {
    setAllocMapPos(i, 0, false);
  }

  // Add filesystem header to allocation map
  for (uint8_t i = 0; i < HEADER_SIZE; i++) {
    setAllocMapPos(i, 1, false);
  }

  // Add filesystem terminator to allocation map
  setAllocMapPos(fs_size-1, 1, false);

  // Recursively mark all files as allocated.
  markInAllocMap(cwd[0], 1, false);
}

/* Print file hierarchy to serial */
void _tree (struct File f, uint8_t indentLevel) {
  if (f.isDir) {printIndent(max(indentLevel-1, 0));} else {printIndent(indentLevel);}
  if (f.isDir) {Serial.print('[');}
  Serial.print(f.address); Serial.print(":"); Serial.print(f.name);
  if (!f.isDir) {
    Serial.print(":");
    for (uint16_t i = f.dataStartAddr; i < f.dataStartAddr+f.dataSize; i++) {
      Serial.print(char(readROM(i)));
    }
  }
  if (f.isDir) {Serial.print(']');}
  Serial.println();

  if (!f.isDir) {
    return;
  }

  struct File subfiles[f.dataSize/2];
  getSubfiles(f, subfiles);
  for (uint8_t i = 0; i < sizeof(subfiles)/sizeof(struct File); i++) {
    _tree(subfiles[i], indentLevel+4);
  }
}
void tree (struct File f) {_tree(f, 0);}

/* Wipe all deallocated memory regions */
void setUnallocated(uint8_t value) {
  for (uint16_t i = 0; i < EEPROM.length(); i++) {
    if (!getAllocMapPos(i)) {
      writeROM(i, value);
    }
  }
}

/* Find a free contiguous memory region with a minimal size */
void findFreeContigMem(uint16_t size, uint16_t *segmentMarker) {
  uint16_t prevSegStartAddr = 0;
  uint16_t prevSegLength = 0;

  uint16_t currentSegStartAddr = 0;
  uint16_t currentSegLength = 0;

  for (uint16_t i = 0; i < EEPROM.length(); i++) {
    if (!getAllocMapPos(i)) {
      currentSegLength++;
    } else {
      // alternative but possibly more fragmented
      // if (currentSegLength >= size && (currentSegLength < prevSegLength || prevSegLength == 0)) {
      if (currentSegLength >= size && (currentSegLength <= prevSegLength || prevSegLength == 0)) {
        prevSegLength = currentSegLength;
        prevSegStartAddr = currentSegStartAddr;
      }
      currentSegLength = 0;
      currentSegStartAddr = i+1;
    }
  }

  segmentMarker[0] = prevSegStartAddr;
  segmentMarker[1] = prevSegLength;
}

/* Create standalone file in memory */
uint16_t createFile(String name, bool isDir, byte *data, uint8_t dataSize) {
  uint16_t newFileSegmentMarker[2];
  uint16_t fileLength = 1+2+name.length()+dataSize;
  findFreeContigMem(fileLength, newFileSegmentMarker);
  if (newFileSegmentMarker[1] < fileLength) {
    Serial.print(F("Error: No free contiguous memory segment >= ")); Serial.print(fileLength); Serial.println(F(" bytes found."));
    Serial.print(F("Larges found segment is ")); Serial.print(newFileSegmentMarker[1]); Serial.print(F(" bytes long at address ")); Serial.println(newFileSegmentMarker[0]);
    return 0;
  }
  uint16_t newFileAddr = newFileSegmentMarker[0];

  byte newFileHeaderByte = 0;
  bitWrite(newFileHeaderByte, 0, isDir);
  writeROM(newFileAddr, newFileHeaderByte);
  writeROM(newFileAddr+1, name.length());
  writeROM(newFileAddr+2, dataSize);
  for (uint8_t i = 0; i < name.length(); i++) {
    writeROM(newFileAddr+3+i, name.charAt(i));
  }
  for (uint8_t i = 0; i < dataSize; i++) {
    writeROM(newFileAddr+3+name.length()+i, data[i]);
    //Serial.println(char(data[i]));
    //Serial.println(newFileAddr+3+name.length()+i);
  }

  // mark new file space as allocated
  for (int16_t i = newFileAddr; i < newFileAddr+fileLength; i++) {
    setAllocMapPos(i, 1, false);
  }

  return newFileAddr;
}

/* Create file and update parent dir */
uint16_t mkfile(String name, bool isDir, byte *data, uint8_t dataSize) {
  struct File f = getFileByName(name);

  if (f.name != F("ERR_FILE_NOT_FOUND")) {
    Serial.print(F("Error: File already exists: ")); Serial.println(name);
    printFile(f);
    return;
  }

  struct File parentDirectory = cwd[cwdPointer];

  // check if parent relocation is neccessary
  //    if so → move parent & alloc two additional bytes at the end but don't write to them yet
  //      also relink parent-parent dir with moved parent dir
  //    if not → only alloc two additional bytes at the end but don't write to them yet
  // create file
  //    if there is an error (no space) → dealloc additional parent dir bytes
  //    else → append new address to parent dir
  // done

  // check if parent dir needs to be reallocated to accomodate the additional subfile address
  if (getAllocMapPos(parentDirectory.dataStartAddr+parentDirectory.dataSize) || getAllocMapPos(parentDirectory.dataStartAddr+parentDirectory.dataSize+1)) {

    // read all parent dir data
    byte parentDirData[parentDirectory.dataSize+2];
    uint8_t j = 0;
    for (uint16_t i = parentDirectory.dataStartAddr; i < parentDirectory.dataStartAddr+parentDirectory.dataSize; i++) {
      parentDirData[j] = readROM(i);
      j++;
    }

    // temporarily set new file addr to 0 until the new address is known
    parentDirData[j] = 0;
    parentDirData[j+1] = 0;

    // recreate parent dir in new location
    uint16_t parentDirNewAddr = createFile(parentDirectory.name, true, parentDirData, parentDirectory.dataSize+2);

    if (parentDirNewAddr == 0) {
      Serial.println(F("Unable to move parent directory. No changes were made."));
      return 0;
    }

    // mark old parent dir space as unallocated
    for (uint16_t i = parentDirectory.address; i < parentDirectory.dataStartAddr+parentDirectory.dataSize; i++) {
      setAllocMapPos(i, 0, false);
    }

    // relink parent-parent dir to moved parent dir
    if (cwdPointer >= 1) {
      struct File parentParentDir = cwd[cwdPointer-1];
      // find old address and overwrite it
      for (uint16_t i = parentParentDir.dataStartAddr; i < parentParentDir.dataStartAddr+parentParentDir.dataSize; i+=2) {
         if (readTwoBytes(i) == parentDirectory.address) {
           writeTwoBytes(i, parentDirNewAddr);
           break;
         }
      }
    } else if (cwdPointer == 0){
      // if the directory being moved is the root dir, the root dir address needs to be updated in the fs header
      writeTwoBytes(3, parentDirNewAddr);
    } else {
      Serial.println(F("Really shouldn't happen :("));
    }

    // update our parent dir File instance
    parentDirectory.address = parentDirNewAddr;
    parentDirectory.dataSize += 2;
    parentDirectory.dataStartAddr = parentDirectory.address+3+parentDirectory.name.length();
  } else {
    // Not moving the parent dir
    setAllocMapPos(parentDirectory.dataStartAddr+parentDirectory.dataSize, 1, false);
    setAllocMapPos(parentDirectory.dataStartAddr+parentDirectory.dataSize+1, 1, false);

    uint8_t prevLength = readROM(parentDirectory.address+2);
    parentDirectory.dataSize = prevLength+2;
    writeROM(parentDirectory.address+2, prevLength+2);
  }

  uint16_t newFileAddr = createFile(name, isDir, data, dataSize);
  //Serial.print(F("Created file at new address: ")); Serial.println(newFileAddr);

  if (newFileAddr == 0) {
    Serial.println(F("Unable to create file. Reverting all changes.."));
    uint8_t prevLength = readROM(parentDirectory.address+2);
    writeROM(parentDirectory.address+2, prevLength-2);
    parentDirectory.dataSize -= 2;
    parentDirectory.dataStartAddr = parentDirectory.address+3+parentDirectory.name.length();

    setAllocMapPos(parentDirectory.dataStartAddr+parentDirectory.dataSize, 1, false);
    setAllocMapPos(parentDirectory.dataStartAddr+parentDirectory.dataSize+1, 1, false);
  } else {
    //Serial.print(F("writing new file address to location: ")); Serial.println(parentDirectory.dataStartAddr+parentDirectory.dataSize-2);
    Serial.println("Created new file successfully.");
    writeTwoBytes(parentDirectory.dataStartAddr+parentDirectory.dataSize-2, newFileAddr);
  }

  cwd[cwdPointer] = parentDirectory;

  return newFileAddr;
}

/* Recursively remove file(s) */
void rm(String name, bool deepRemove) {
  struct File f = getFileByName(name);

  if (f.name == F("ERR_FILE_NOT_FOUND")) {
    Serial.println(F("Error: File not found"));
    return;
  }

  // Recursively mark file (and subfiles) as unused in allocation map
  markInAllocMap(f, 0, deepRemove);

  // get subfile addresses of parent dir (to remove the deleted file from them)
  struct File parentDirectory = cwd[cwdPointer];
  uint16_t parentDirectorySubfilesAddr[parentDirectory.dataSize/2];
  uint8_t parentSubfileIndexOfDeleted;

  uint16_t j = 0;
  for (uint16_t i = parentDirectory.dataStartAddr; i < parentDirectory.dataStartAddr+parentDirectory.dataSize; i+=2) {
    parentDirectorySubfilesAddr[j] = readTwoBytes(i);
    if (parentDirectorySubfilesAddr[j] == f.address) {
      parentSubfileIndexOfDeleted = j;
    }
    j++;
  }

  // decrease parent dir subfile count
  int8_t prevParentDataSize = parentDirectory.dataSize;
  parentDirectory.dataSize -= 2;
  writeROM(parentDirectory.address+2, prevParentDataSize-2);

  // switch last parent dir subfile addr data with deleted subfile addr data
  writeTwoBytes(parentDirectory.dataStartAddr+parentSubfileIndexOfDeleted*2, parentDirectorySubfilesAddr[prevParentDataSize/2-1]);

  // mark last parent dir subfile addr data as free
  setAllocMapPos(parentDirectory.dataStartAddr+parentDirectory.dataSize, 0, deepRemove);
  setAllocMapPos(parentDirectory.dataStartAddr+parentDirectory.dataSize+1, 0, deepRemove);

  // update cwd parent dir File instance (to update data size there)
  cwd[cwdPointer] = parentDirectory;
  Serial.print(F("Removed ")); printCwd(); Serial.println(f.name);
}

/* Print file content to serial */
void cat(String name) {
  struct File f = getFileByName(name);
  if (f.name == F("ERR_FILE_NOT_FOUND")) {
    Serial.println(F("File not found."));
  }
  for (uint16_t i = f.dataStartAddr; i < f.dataStartAddr+f.dataSize; i++) {
    Serial.print(char(readROM(i)));
  }
  Serial.println();
}

/* not working, doesn't adjust parent dir address/link & very inefficient (1-byte displ)*/
/*void defrag() {
  struct File currentCwd = cwd[cwdPointer];
  struct File subfiles[currentCwd.dataSize/2];
  getSubfiles(currentCwd, subfiles);

  bool movedAtLeastOne = false;

  do {
    Serial.print(".");
    movedAtLeastOne = false;
    readfs();
    for (uint8_t i = 0; i < sizeof(subfiles)/sizeof(struct File)-1; i++) {
      if (getAllocMapPos(subfiles[i].address-1) == 1) {
        continue;
      }
      Serial.print(F("Moving: "));
      Serial.println(subfiles[i].name);

      // move
      setAllocMapPos(subfiles[i].address-1, 1, false);
      setAllocMapPos(subfiles[i].dataStartAddr+subfiles[i].dataSize-1, 0, false);

      byte fileData[subfiles[i].dataSize+1+2+subfiles[i].name.length()];
      uint8_t j = 0;
      for (uint16_t i = subfiles[i].address; i < subfiles[i].address+subfiles[i].dataSize+1+2+subfiles[i].name.length(); i++) {
        fileData[j] = readROM(i);
        j++;
      }

      j = 0;
      for (uint16_t i = subfiles[i].address-1; i < subfiles[i].address+subfiles[i].dataSize+1+2+subfiles[i].name.length()-1; i++) {
        writeROM(i, fileData[j]);
        j++;
      }

      movedAtLeastOne = true;
    }
  } while (movedAtLeastOne);
  Serial.println();
}*/

/* Print hr memory alloc stats to serial */
void printMemStats() {
  uint16_t sum = 0;
  for (uint16_t i = 0; i < EEPROM.length(); i++) {
    sum += getAllocMapPos(i);
  }
  Serial.print("[");
  for (uint8_t i = 0; i < 30; i++) {
    if (i < (int) ((((float) sum)/EEPROM.length())*30)) {
      Serial.print("#");
    } else {
      Serial.print(" ");
    }
  }
  Serial.print("]\t");
  Serial.print(sum); Serial.print("/"); Serial.print(EEPROM.length()); Serial.println(F(" bytes allocated."));
}

/* Check whether a filesystem is readable and if so, read its metadata
and return true, otherwise return false */
bool readfs() {
  if (readROM(0) != 0xFF) {
    Serial.println(F("Error: 'Filesystem header not detected.'"));
    return false;
  }
  fs_size = readTwoBytes(1);
  if (fs_size < 16) {
    Serial.println(F("Warning: Filesystem size may be corrupted or filesystem too small."));
  }
  if (readROM(fs_size-1) != 0xEE) {
    Serial.println(F("Error: 'Filesystem header found but terminator overwritten. Ignoring..'"));
  }
  uint16_t rootDirAddr = readTwoBytes(3);
  cwd[0] = readFile(rootDirAddr);
  cwdPointer = 0;
  createAllocMap();
  Serial.print(F("Found filesystem of ")); Serial.print(fs_size); Serial.println(F(" bytes starting from address 0"));
  printMemStats();
  return true;
}

/* Create new filesystem starting at position 0 with length 'size' [1, 65536]
return whether the operation was successful */
bool mkfs(uint16_t size) {
  if (size < 16) {
    return false;
  }

  // write fs header
  writeROM(0, 0xFF); // fs metadata
  writeROM(1, highByte(size)); // fs size
  writeROM(2, lowByte(size));
  writeROM(3, 0); // root dir address
  writeROM(4, 5);

  // write root dir
  writeROM(5, 0b00000001);
  writeROM(6, 4);
  writeROM(7, 0);
  writeROM(8, 'r');
  writeROM(9, 'o');
  writeROM(10, 'o');
  writeROM(11, 't');

  // write fs terminator
  writeROM(size-1, 0xEE);
  return true;
}

void setup() {
  Serial.begin(2000000);
  while (!Serial) {}
  //readfs();
}


void loop() {
  // ugly command parsing logic :/
  if (Serial.available() > 0) {
    commandString = Serial.readStringUntil('\n');

    for (uint8_t i = 0; i < sizeof(command)/sizeof(String); i++) {
      command[i] = "";
    }

    uint8_t arrayIndex = 0;
    bool readingData = false;
    for (uint16_t i = 0; i < commandString.length(); i++) {
      if (commandString.charAt(i) == '>' && !readingData){
        readingData = true;
      } else if (commandString.charAt(i) == ' ' && !readingData) {
        arrayIndex++;
      } else if (commandString.charAt(i) != '\n') {
        command[arrayIndex] += commandString.charAt(i);
      }
    }

    if (command[0] == F("readfs")) {
      bool valid = readfs();
    } else if (command[0] == F("ping")) {
      Serial.println(F("pong"));
    } else if (command[0] == F("mkfs")) {
      bool result = mkfs(command[1].toInt());
      if (result) {
        readfs();
        Serial.println(F("mkfs successful"));
      } else {
        Serial.println(F("mkfs unsuccessful"));
      }
    } else if (command[0] == F("wipe")) {
      wipe();
      Serial.println(F("wiping successful"));
    } else if (command[0] == F("memdump")) {
      if (command[1] == "ignbuf") {
        memdump(true);
      } else {
        memdump(false);
      }
    } else if (command[0] == F("allocdump")) {
      dumpAllocMap();
    } else if (command[0] == F("cwd")) {
      printCwd();
      Serial.println();
    } else if (command[0] == F("tree")) {
      tree(cwd[cwdPointer]);
    }  else if (command[0] == F("ls")) {
      ls();
    } else if (command[0] == F("cd")) {
      cd(command[1]);
    } else if (command[0] == F("mkallocmap")) {
      createAllocMap();
    } else if (command[0] == F("cat")) {
      cat(command[1]);
    } else if (command[0] == F("rm")) {
      if (command[2] == F("wipe")) {
        rm(command[1], true);
      } else {
        rm(command[1], false);
      }
    } else if (command[0] == F("wipeunalloc")) {
      setUnallocated(command[1].toInt());
    } else if (command[0] == F("mkdir")) {
      byte data[0];
      mkfile(command[1], true, data, 0);
    } else if (command[0] == F("mkfile")) {
      byte data[command[2].length()];
      command[2].toCharArray(data, command[2].length()+1);
      mkfile(command[1], false, data, command[2].length());
    } else if (command[0] == F("memstats")) {
      printMemStats();
    }/* else if (command[0] == F("flush")) {
      flushBuffer();
    } else if (command[0] == F("writecycles")) {
      Serial.println(totalWriteCycles);
    }*/
  }
}
