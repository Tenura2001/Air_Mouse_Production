#include <BleCombo.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <up_down_inferencing.h> // YOUR EDGE IMPULSE LIBRARY

// ================== CONFIGURATION ==================
#define SPEED 10   // Adjust mouse speed
#define PIXEL_PIN 2
#define NUM_PIXELS 1

// AI MODEL SETTINGS
#define EI_SAMPLING_FREQ_HZ 60
#define EI_SAMPLING_INTERVAL_MS (1000 / (EI_SAMPLING_FREQ_HZ + 1))
#define CONFIDENCE_THRESHOLD 0.8  // 80% confidence required

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

// AI Variables
float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
size_t feature_ix = 0;
static unsigned long last_sample_time = 0;
unsigned long last_gesture_time = 0; 

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
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); 

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Ready!");
  display.display();

  delay(1000);
}

// ================== MAIN LOOP =======================
void loop() {
  bool connected = Keyboard.isConnected();

  // Update LED and OLED status
  setPixelColor(connected, mouseMode);
  updateDisplay(connected, mouseMode);

  // 1. HANDLE BUTTONS & MODE SWITCHING
  if (connected) {
    handleModeSwitch();
    handleButtons(mouseMode); 
  }

  // 2. SENSOR SAMPLING LOOP (Runs at 60Hz)
  if (millis() > last_sample_time + EI_SAMPLING_INTERVAL_MS) {
    last_sample_time = millis();

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    if (connected) {
        
        // ============ LOGIC SPLIT BASED ON MODE ============
        
        if (mouseMode) {
            // --- MOUSE MODE ---
            // 1. Move Mouse
            Mouse.move(g.gyro.z * -SPEED, g.gyro.y * SPEED);
            
            // 2. Clear AI Buffer (So we don't accidentally trigger gestures)
            feature_ix = 0; 
        } 
        else {
            // --- KEYBOARD MODE (AI ENABLED) ---
            
            // 1. Fill Buffer
            features[feature_ix++] = a.acceleration.x;
            features[feature_ix++] = a.acceleration.y;
            features[feature_ix++] = a.acceleration.z;

            // 2. Run Inference when buffer is full
            if (feature_ix == EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
                runInference();
                feature_ix = 0; // Reset buffer
            }
        }
    }
  }
}

// ================== INFERENCE LOGIC =================
void runInference() {
    signal_t signal;
    ei_impulse_result_t result;

    int err = numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
    if (err != 0) {
        Serial.printf("Err creating signal (%d)\n", err);
        return;
    }

    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, true);
    if (res != 0) return;

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > CONFIDENCE_THRESHOLD) {
            
            // Debounce to prevent double triggering
            if (millis() - last_gesture_time > 500) {
                
                if (strcmp(result.classification[ix].label, "up") == 0) {
                    Serial.println("AI: Volume UP");
                    Keyboard.write(KEY_MEDIA_VOLUME_UP);
                    displayAction("AI: Vol UP");
                    last_gesture_time = millis();
                }
                
                else if (strcmp(result.classification[ix].label, "down") == 0) {
                    Serial.println("AI: Volume DOWN");
                    Keyboard.write(KEY_MEDIA_VOLUME_DOWN);
                    displayAction("AI: Vol DOWN");
                    last_gesture_time = millis();
                }
            }
        }
    }
}

// ================== BUTTON HANDLER ==================
void handleButtons(bool isMouseMode) {
    if (isMouseMode) {
      // Mouse Mode Buttons
      if (digitalRead(BTN1) == LOW) { Mouse.click(MOUSE_LEFT); delay(200); }
      if (digitalRead(BTN2) == LOW) { Mouse.click(MOUSE_RIGHT); delay(200); }
      if (digitalRead(BTN3) == LOW) { Mouse.move(0, 0, 1); delay(200); }
      if (digitalRead(BTN4) == LOW) { Mouse.move(0, 0, -1); delay(200); }
    } else {
      // Keyboard Mode Buttons
      if (digitalRead(BTN3) == LOW) { Keyboard.write(KEY_MEDIA_VOLUME_UP); delay(200); }
      if (digitalRead(BTN4) == LOW) { Keyboard.write(KEY_MEDIA_VOLUME_DOWN); delay(200); }
      if (digitalRead(BTN1) == LOW) { Keyboard.write(KEY_MEDIA_PLAY_PAUSE); delay(300); }
      if (digitalRead(BTN2) == LOW) { Keyboard.write(KEY_MEDIA_NEXT_TRACK); delay(300); }
    }
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
      // Reset AI buffer when switching to prevent old data from triggering
      feature_ix = 0; 
      Serial.print("Mode: ");
      Serial.println(mouseMode ? "MOUSE" : "KEYBOARD (AI)");
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
    display.setTextSize(1);
    display.setCursor(0, 45);
    display.println("AI Enabled");
  }

  display.display();
}

void displayAction(const char* msg) {
    display.clearDisplay();
    display.setCursor(0, 20);
    display.setTextSize(2);
    display.println(msg);
    display.display();
}

void ei_printf(const char *format, ...) {
  static char print_buf[1024] = { 0 };
  va_list args;
  va_start(args, format);
  int r = vsnprintf(print_buf, sizeof(print_buf), format, args);
  va_end(args);
  if (r > 0) {
    Serial.write(print_buf);
  }
}
