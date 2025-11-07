#include <BleCombo.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

// ================== CONFIGURATION ==================
#define SPEED 10   // Adjust mouse speed
#define PIXEL_PIN 2
#define NUM_PIXELS 1

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ================== BUTTON PINS =====================
#define BTN_SELECT 35  // Mode switch button
#define BTN1       33
#define BTN2       27
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
  Serial.println("Starting...");

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
  pixel.show();
  setPixelColor(false, false);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 not found!");
    while (1);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();

  // MPU6050
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("MPU6050 Error!");
    display.display();
    while (1) delay(10);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Setup complete!");
  display.display();

  Serial.println("Setup complete!");
  delay(1000);
}

// ================== MAIN LOOP =======================
void loop() {
  bool connected = Keyboard.isConnected();

  // Update LED and OLED status
  setPixelColor(connected, mouseMode);
  updateDisplay(connected, mouseMode);

  if (connected) {
    handleModeSwitch();

    if (mouseMode) {
      // ================= Mouse Mode =================
      sensors_event_t a, g, temp;
      mpu.getEvent(&a, &g, &temp);

      Mouse.move(g.gyro.z * -SPEED, g.gyro.y * SPEED);

      if (digitalRead(BTN1) == LOW) {
        Mouse.click(MOUSE_LEFT);
        Serial.println("Mouse Left Click");
        delay(200);
      }
      if (digitalRead(BTN2) == LOW) {
        Mouse.click(MOUSE_RIGHT);
        Serial.println("Mouse Right Click");
        delay(200);
      }
      if (digitalRead(BTN3) == LOW) {
        Mouse.move(0, 0, 1); // Scroll up
        Serial.println("Mouse: Scroll Up");
        delay(200);
      }
      if (digitalRead(BTN4) == LOW) {
        Mouse.move(0, 0, -1); // Scroll down
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
        Serial.println("Play/Pause");
        delay(300);
      }
      if (digitalRead(BTN2) == LOW) {
        Keyboard.write(KEY_MEDIA_NEXT_TRACK);
        Serial.println("Next Track");
        delay(300);
      }
    }
  }

  delay(20);
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
      delay(500);
    }
  } else {
    buttonHeld = false;
  }
}

// ================== PIXEL LED FUNCTION ===============
void setPixelColor(bool connected, bool mouseMode) {
  if (!connected) {
    pixel.setPixelColor(0, pixel.Color(25, 0, 0)); // 🔴 Not connected
  } else if (mouseMode) {
    pixel.setPixelColor(0, pixel.Color(0, 0, 25)); // 🔵 Mouse Mode
  } else {
    pixel.setPixelColor(0, pixel.Color(0, 25, 0)); // 🟢 Keyboard Mode
  }
  pixel.show();
}

// ================== OLED DISPLAY FUNCTION ============
void updateDisplay(bool connected, bool mouseMode) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (connected) {
    display.println("BLE Connected");
  } else {
    display.println("Not Connected");
  }

  display.setCursor(0, 20);
  display.setTextSize(2);
  if (mouseMode) {
    display.println("MOUSE");
  } else {
    display.println("KEYBOARD");
  }

  display.display();
}
