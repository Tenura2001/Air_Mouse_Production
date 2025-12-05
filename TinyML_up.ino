#include <BleCombo.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <up_down_inferencing.h> // YOUR EDGE IMPULSE LIBRARY

// ================== FREERTOS & DUAL CORE SETTINGS ==================
TaskHandle_t TaskCore0; // Handle for the AI/Display task
SemaphoreHandle_t i2cMutex; // Mutex to protect I2C bus (Wire)

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

// ================== SHARED VARIABLES =======================
// Volatile ensures both cores see the updated value immediately
volatile bool mouseMode = true;
volatile bool triggerInference = false; // Flag to tell Core 0 to run AI
volatile bool isConnected = false;

// Button Debouncing Variables
unsigned long lastBtnPress[5] = {0, 0, 0, 0, 0}; // Store last press time for each button
const int debounceDelay = 200; // 200ms debounce without blocking

// AI Variables (Protected)
float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
size_t feature_ix = 0;
unsigned long last_sample_time = 0;
unsigned long last_gesture_time = 0; 

// ================== FUNCTION DECLARATIONS ==================
void core0Task(void * pvParameters);
void handleButtonsNonBlocking();
void handleModeSwitchNonBlocking();
void runInference();
void setPixelColor(bool connected, bool mode);
void updateDisplayLazy(bool connected, bool mode);

// ================== SETUP ===========================
void setup() {
  Serial.begin(115200);
  
  // 1. Initialize Mutex for I2C protection
  i2cMutex = xSemaphoreCreateMutex();

  // 2. Initialize Hardware
  Keyboard.begin();
  Mouse.begin();

  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);

  pixel.begin();
  pixel.setBrightness(50);
  setPixelColor(false, true);

  // 3. I2C Initialization (Protected)
  // We take the mutex even in setup just to be safe, though not strictly needed here yet
  xSemaphoreTake(i2cMutex, portMAX_DELAY);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 not found!");
    for(;;);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Booting Dual Core...");
  display.display();

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    while (1) delay(10);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); 

  xSemaphoreGive(i2cMutex); // Release I2C

  // 4. Launch Core 0 Task (AI & Display)
  // core0Task function, "AI_Task", Stack size 10000, params NULL, Priority 1, TaskHandle, Core 0
  xTaskCreatePinnedToCore(
      core0Task,   
      "AI_Display", 
      10000,       
      NULL,        
      1,           
      &TaskCore0,  
      0);          

  delay(500);
}

// ================== CORE 1: HIGH SPEED LOOP (MOUSE) ==================
// This loop runs on Core 1 by default. It handles Movement & Data Collection.
void loop() {
  isConnected = Keyboard.isConnected(); // Check BLE status

  // 1. Handle Inputs (Zero Delay)
  if (isConnected) {
    handleModeSwitchNonBlocking();
    handleButtonsNonBlocking();
  }

  // 2. High Frequency Sensor Sampling
  if (millis() > last_sample_time + EI_SAMPLING_INTERVAL_MS) {
    last_sample_time = millis();

    sensors_event_t a, g, temp;
    
    // CRITICAL: Lock I2C before reading sensor
    if (xSemaphoreTake(i2cMutex, (TickType_t) 10) == pdTRUE) {
      mpu.getEvent(&a, &g, &temp);
      xSemaphoreGive(i2cMutex); // Unlock immediately after reading
    } else {
      // If busy, skip this sample to prevent lag
      return; 
    }

    if (isConnected) {
        if (mouseMode) {
            // --- MOUSE MODE: Direct Movement ---
            // Invert axis if needed based on mounting
            Mouse.move(g.gyro.z * -SPEED, g.gyro.y * SPEED);
            feature_ix = 0; // Keep AI buffer empty
        } 
        else {
            // --- KEYBOARD MODE: Fill AI Buffer ---
            if (feature_ix < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
                features[feature_ix++] = a.acceleration.x;
                features[feature_ix++] = a.acceleration.y;
                features[feature_ix++] = a.acceleration.z;

                // When buffer is full, flag Core 0 to run inference
                if (feature_ix == EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
                    triggerInference = true; 
                    // Note: We do NOT reset feature_ix here. 
                    // We wait for Core 0 to process it.
                }
            }
        }
    }
  }
  
  // Small yield to prevent watchdog trigger, but keep it tight
  delay(1); 
}

// ================== CORE 0: HEAVY LIFTING (AI & SCREEN) ==================
void core0Task(void * pvParameters) {
  Serial.print("AI/Display running on Core: ");
  Serial.println(xPortGetCoreID());

  static bool lastMode = !mouseMode; // Force update on start
  static bool lastConn = !isConnected;

  for(;;) {
    // 1. AI INFERENCE
    // Only run if triggered and we are in keyboard mode
    if (triggerInference && !mouseMode) {
        runInference(); 
        // Reset buffer logic
        feature_ix = 0; 
        triggerInference = false;
    }

    // 2. DISPLAY & LED UPDATES
    // We only update if something changed to save I2C bandwidth
    if (mouseMode != lastMode || isConnected != lastConn) {
        // Update LED
        setPixelColor(isConnected, mouseMode);
        
        // Update OLED (Needs Mutex)
        if (xSemaphoreTake(i2cMutex, (TickType_t) 100) == pdTRUE) {
            updateDisplayLazy(isConnected, mouseMode);
            xSemaphoreGive(i2cMutex);
        }
        
        lastMode = mouseMode;
        lastConn = isConnected;
    }

    // Slow down this task slightly to let Core 1 breathe if sharing resources
    // 20ms delay means display/AI checks happen at ~50Hz max
    vTaskDelay(20 / portTICK_PERIOD_MS); 
  }
}

// ================== LOGIC HELPERS ==================

void runInference() {
    signal_t signal;
    ei_impulse_result_t result;

    // Convert buffer to signal
    int err = numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
    if (err != 0) return;

    // Run Classifier (This is the slow part! Now safely on Core 0)
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, true);
    if (res != 0) return;

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > CONFIDENCE_THRESHOLD) {
            
            if (millis() - last_gesture_time > 1000) { // Increased cooldown to 1s
                
                if (strcmp(result.classification[ix].label, "up") == 0) {
                    Serial.println("AI: UP");
                    Keyboard.write(KEY_MEDIA_VOLUME_UP);
                    
                    if (xSemaphoreTake(i2cMutex, 100)) {
                      displayAction("Vol UP");
                      xSemaphoreGive(i2cMutex);
                    }
                    last_gesture_time = millis();
                }
                else if (strcmp(result.classification[ix].label, "down") == 0) {
                    Serial.println("AI: DOWN");
                    Keyboard.write(KEY_MEDIA_VOLUME_DOWN);
                    
                    if (xSemaphoreTake(i2cMutex, 100)) {
                      displayAction("Vol DOWN");
                      xSemaphoreGive(i2cMutex);
                    }
                    last_gesture_time = millis();
                }
            }
        }
    }
}

// Non-blocking Button Handler
void handleButtonsNonBlocking() {
    unsigned long currentMillis = millis();

    if (mouseMode) {
        // BTN1: Left Click
        if (digitalRead(BTN1) == LOW) {
            if (currentMillis - lastBtnPress[0] > debounceDelay) {
                Mouse.click(MOUSE_LEFT);
                lastBtnPress[0] = currentMillis;
            }
        }
        // BTN2: Right Click
        if (digitalRead(BTN2) == LOW) {
            if (currentMillis - lastBtnPress[1] > debounceDelay) {
                Mouse.click(MOUSE_RIGHT);
                lastBtnPress[1] = currentMillis;
            }
        }
        // BTN3: Scroll Up
        if (digitalRead(BTN3) == LOW) {
            // Note: Continuous scroll might be preferred without full debounce
            // but for safety we apply partial debounce or just check pin
            if (currentMillis - lastBtnPress[2] > 50) { // Fast repeat allowed
                Mouse.move(0, 0, 1);
                lastBtnPress[2] = currentMillis;
            }
        }
        // BTN4: Scroll Down
        if (digitalRead(BTN4) == LOW) {
             if (currentMillis - lastBtnPress[3] > 50) { // Fast repeat allowed
                Mouse.move(0, 0, -1);
                lastBtnPress[3] = currentMillis;
            }
        }
    } else {
        // Keyboard Mode
        
        // BTN3: Volume Up
        if (digitalRead(BTN3) == LOW) {
            if (currentMillis - lastBtnPress[2] > debounceDelay) {
                Keyboard.write(KEY_MEDIA_VOLUME_UP);
                lastBtnPress[2] = currentMillis;
            }
        }
        // BTN4: Volume Down
        if (digitalRead(BTN4) == LOW) {
            if (currentMillis - lastBtnPress[3] > debounceDelay) {
                Keyboard.write(KEY_MEDIA_VOLUME_DOWN);
                lastBtnPress[3] = currentMillis;
            }
        }
        // BTN1: Play/Pause
        if (digitalRead(BTN1) == LOW) {
            if (currentMillis - lastBtnPress[0] > debounceDelay) {
                Keyboard.write(KEY_MEDIA_PLAY_PAUSE);
                lastBtnPress[0] = currentMillis;
            }
        }
        // BTN2: Next Track
        if (digitalRead(BTN2) == LOW) {
            if (currentMillis - lastBtnPress[1] > debounceDelay) {
                Keyboard.write(KEY_MEDIA_NEXT_TRACK);
                lastBtnPress[1] = currentMillis;
            }
        }
    }
}

void handleModeSwitchNonBlocking() {
    static unsigned long pressStart = 0;
    static bool isHolding = false;
    
    if (digitalRead(BTN_SELECT) == LOW) {
        if (!isHolding) {
            pressStart = millis();
            isHolding = true;
        }
    } else {
        // Button released
        if (isHolding) {
            if (millis() - pressStart > 50) { // Minimum 50ms press
                mouseMode = !mouseMode;
                feature_ix = 0; // Reset AI
            }
            isHolding = false;
        }
    }
}

void setPixelColor(bool connected, bool mouseMode) {
    // Pixel is not I2C, usually safe to call, but better to keep short
    if (!connected) pixel.setPixelColor(0, pixel.Color(25, 0, 0));
    else if (mouseMode) pixel.setPixelColor(0, pixel.Color(0, 0, 25));
    else pixel.setPixelColor(0, pixel.Color(0, 25, 0));
    pixel.show();
}

void updateDisplayLazy(bool connected, bool mouseMode) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(connected ? "BLE: ON" : "BLE: OFF");
    
    display.setCursor(0, 20);
    display.setTextSize(2);
    display.println(mouseMode ? "MOUSE" : "AI KEYS");
    display.display();
}

void displayAction(const char* msg) {
    // This is called from AI task (Core 0), already holds Mutex if called correctly
    display.clearDisplay();
    display.setCursor(0, 20);
    display.setTextSize(2);
    display.println(msg);
    display.display();
    
    // We force a redraw of main menu after a short while?
    // In a real dual core app, we might use a state variable to revert the screen later.
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
