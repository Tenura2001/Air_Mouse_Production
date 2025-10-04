#include <BleCombo.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

// MPU
Adafruit_MPU6050 mpu;
#define SPEED 10   // Adjust mouse speed

// Define button pins
#define BTN_SELECT  18   // Mode switch button
#define BTN1        36   // Action button 1
#define BTN2        23   // Action button 2
#define BTN3   34   
#define BTN4   19   


bool mouseMode = true;   // Start in mouse mode

unsigned long buttonPressStart = 0;
bool buttonHeld = false;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting work...");

  // BLE
  Keyboard.begin();
  Mouse.begin();

  // Buttons
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);
  pinMode(2,OUTPUT);

  // MPU6050
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    while (1) delay(10);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);

  Serial.println("Setup complete!");
}

void loop() {
  if (Keyboard.isConnected()) {
    handleModeSwitch();

    if (mouseMode) {
      digitalWrite(2,LOW);
      // ================= Mouse Mode =================
      sensors_event_t a, g, temp;
      mpu.getEvent(&a, &g, &temp);

      // Mouse movement with gyro
      Mouse.move(g.gyro.z * -SPEED, g.gyro.x * -SPEED);

      // Button controls in Mouse Mode
      if (digitalRead(BTN1) == LOW) {
        Mouse.click(MOUSE_LEFT); // Left click
        Serial.println("Mouse Left Click");
        delay(200);
      }
      if (digitalRead(BTN2) == LOW) {
        Mouse.click(MOUSE_RIGHT); // Right click
        Serial.println("Mouse Right Click");
        delay(200);
      }

      if (digitalRead(BTN3) == LOW) {
        Mouse.move(0, 0, 1); // scroll up
        Serial.println("Mouse: Scroll Up");
        delay(200);
      }

      if (digitalRead(BTN4) == LOW) {
        Mouse.move(0, 0, -1); // scroll down
        Serial.println("Mouse: Scroll Down");
        delay(200);
      }
    } else {
      digitalWrite(2,HIGH);
    
      // ================= Keyboard/Media Mode =================
      if (digitalRead(BTN3) == LOW) {
        Keyboard.write(KEY_MEDIA_VOLUME_UP); // Volume up
        Serial.println("Volume Up");
        delay(200);
      }
      if (digitalRead(BTN4) == LOW) {
        Keyboard.write(KEY_MEDIA_VOLUME_DOWN); // Volume down
        Serial.println("Volume Down");
        delay(200);
      }
      if (digitalRead(BTN1) == LOW) {
        Keyboard.write(KEY_MEDIA_PLAY_PAUSE);
        Serial.println("Keyboard: Play/Pause");
        delay(300);
      }
      if (digitalRead(BTN2) == LOW) {
        Keyboard.write(KEY_MEDIA_NEXT_TRACK);
        Serial.println("Keyboard: Next Track");
        delay(300);
      }
    }
  }

  delay(20); // short loop delay
}

// ==================================================
// Handle Mode Switching with BTN_SELECT (3s hold)
// ==================================================
void handleModeSwitch() {
  if (digitalRead(BTN_SELECT) == LOW) {
    if (!buttonHeld) {
      buttonPressStart = millis();
      buttonHeld = true;
    } else {
      if (millis() - buttonPressStart > 2000) {
        mouseMode = !mouseMode;  // Toggle mode
        Serial.print("Mode changed to: ");
        Serial.println(mouseMode ? "MOUSE" : "KEYBOARD");
        delay(500); // prevent multiple toggles
      }
    }
  } else {
    buttonHeld = false;
  }
}
