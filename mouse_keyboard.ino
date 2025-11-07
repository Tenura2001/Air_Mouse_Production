#include <BleCombo.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>

// ================== CONFIGURATION ==================
#define SPEED 10   // Adjust mouse speed
#define PIXEL_PIN 2
#define NUM_PIXELS 1  // One NeoPixel

// ================== BUTTON PINS =====================
#define BTN_SELECT 35  // Mode switch button
#define BTN1       33  // Action button 1
#define BTN2       27  // Action button 2
#define BTN3       34
#define BTN4       32

// ================== OBJECTS =========================
Adafruit_MPU6050 mpu;
Adafruit_NeoPixel pixel(NUM_PIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ================== VARIABLES =======================
bool mouseMode = true;   // Start in mouse mode
unsigned long buttonPressStart = 0;
bool buttonHeld = false;

// ================== SETUP ===========================
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

  // Pixel LED
  pixel.begin();
  pixel.show(); // Initialize all pixels to 'off'
  setPixelColor(false, false); // Default to red (not connected)

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

// ================== MAIN LOOP =======================
void loop() {
  bool connected = Keyboard.isConnected();

  // Update LED color based on connection and mode
  setPixelColor(connected, mouseMode);

  if (connected) {
    handleModeSwitch();

    if (mouseMode) {
      // ================= Mouse Mode =================
      sensors_event_t a, g, temp;
      mpu.getEvent(&a, &g, &temp);

      // Mouse movement with gyro
      Mouse.move(g.gyro.z * -SPEED, g.gyro.y * SPEED);

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
      // ================= Keyboard/Media Mode =================
      if (digitalRead(BTN3) == LOW) {
        Keyboard.write(KEY_MEDIA_VOLUME_UP);
        Serial.println("Volume Up");
        delay(200);
      }
      if (digitalRead(BTN4) == LOW) {
        Keyboard.write(KEY_MEDIA_VOLUME_DOWN);
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

// ================== MODE SWITCH =====================
void handleModeSwitch() {
  if (digitalRead(BTN_SELECT) == LOW) {
    if (!buttonHeld) {
      buttonPressStart = millis();
      buttonHeld = true;
    } else {
      // Toggle mode
      mouseMode = !mouseMode;
      Serial.print("Mode changed to: ");
      Serial.println(mouseMode ? "MOUSE" : "KEYBOARD");
      delay(500); // prevent multiple toggles
    }
  } else {
    buttonHeld = false;
  }
}

// ================== PIXEL LED FUNCTION ===============
void setPixelColor(bool connected, bool mouseMode) {
  if (!connected) {
    // 🔴 Red = Not connected
    pixel.setPixelColor(0, pixel.Color(10, 0, 0));
  } else if (mouseMode) {
    // 🔵 Blue = Mouse mode
    pixel.setPixelColor(0, pixel.Color(0, 0, 10));
  } else {
    // 🟢 Green = Keyboard mode
    pixel.setPixelColor(0, pixel.Color(0, 10, 0));
  }
  pixel.show();
}
