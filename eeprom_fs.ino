#include <EEPROM.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  for (int addr = 0; addr < EEPROM.length(); addr++) {
    if (addr % 32 == 0) {
      Serial.println();
    }
    byte value = EEPROM.read(addr);
    Serial.print(value); Serial.print(',');
  }
}

void loop() {

}
