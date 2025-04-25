#include <SPIFFS.h>
#include <FS.h>

void setup() {
  Serial.begin(115200);
  delay(3000); // Wait 3 seconds
  Serial.println("Booting ...");

  // Mount SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully.");

  // Write to a file
  File file = SPIFFS.open("/logs.txt", FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file for writing");
    return;
  }
  file.println("You are using local storage!");
  file.close();
  Serial.println("File written.");

  // Read the file
  file = SPIFFS.open("/logs.txt");
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.println("File Content:");
  while(file.available()){
    Serial.write(file.read());
  }
  file.close();
}

void loop() {
    Serial.println("Hello World!");
    delay(1000);
  
}
