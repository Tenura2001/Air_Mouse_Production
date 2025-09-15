#include <BleKeyboard.h>  // Include BLE Keyboard library

const int VOL_UP = 13;  // the number of the pushbutton pin
const int VOL_DN = 14;  // the number of the pushbutton pin


BleKeyboard bleKeyboard("ESP32_VOL_CONTROL", "ESP32", 100); // Bluetooth HID device name

void setup() {
  Serial.begin(115200);
  pinMode(VOL_UP, INPUT_PULLUP);
  pinMode(VOL_DN, INPUT_PULLUP);
  Serial.println("Starting ESP32 Bluetooth HID...");

  bleKeyboard.begin(); // Start HID mode

  while (!bleKeyboard.isConnected()) {  // Wait for phone to connect
    Serial.println("Waiting for phone connection...");
    delay(1000);
  }

  Serial.println("Phone Connected!");
}

void loop() {
  int VOL_UP_State = digitalRead(VOL_UP);
  int VOL_DN_State = digitalRead(VOL_DN);
  if (bleKeyboard.isConnected()) {

    if(VOL_UP_State == LOW)
    {
      Serial.println("Sending Volume Up Command...");
      bleKeyboard.write(KEY_MEDIA_VOLUME_UP); // Volume Up
    }

    if(VOL_DN_State == LOW)
    {
      Serial.println("Sending Volume Down Command...");
      bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN); // Volume Down
    }
    delay(200);

  }
}
