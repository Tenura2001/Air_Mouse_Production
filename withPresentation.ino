#include <BleCombo.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <up_down_inferencing.h> // YOUR EDGE IMPULSE LIBRARY

// ================== FREERTOS & DUAL CORE SETTINGS ==================
TaskHandle_t TaskCore0;     // Handle for the AI/Display task
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

// ================== MODES ===========================
enum SystemMode {
  MODE_MOUSE = 0,
  MODE_MEDIA = 1,
  MODE_MEDIA_AI = 2,
  MODE_PRESENTATION = 3
};

// ================== OBJECTS =========================
Adafruit_MPU6050 mpu;
Adafruit_NeoPixel pixel(NUM_PIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ================== SHARED VARIABLES =======================
// Volatile ensures both cores see the updated value immediately
volatile int currentMode = MODE_MOUSE; 
volatile bool triggerInference = false; // Flag to tell Core 0 to run AI
volatile bool isConnected = false;

// Presentation Timer Variables (Shared)
volatile bool timerRunning = false;
volatile long timerRemaining = 600000; // 10 Minutes in ms (default)
const long TIMER_START_VALUE = 600000; 

// Button Debouncing Variables
unsigned long lastBtnPress[5] = {0, 0, 0, 0, 0}; 
const int debounceDelay = 200; 

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
void setPixelColor(bool connected, int mode);
void updateDisplayLazy(bool connected, int mode);
void displayAction(const char* msg);

// ================== SETUP ===========================
void setup() {
  Serial.begin(115200);
  
  // 1. Initialize Mutex
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
  setPixelColor(false, MODE_MOUSE);

  // 3. I2C Initialization (Protected)
  xSemaphoreTake(i2cMutex, portMAX_DELAY);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 not found!");
    for(;;);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Booting System...");
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
  xTaskCreatePinnedToCore(core0Task, "AI_Display", 10000, NULL, 1, &TaskCore0, 0);           
  delay(500);
}

// ================== CORE 1: INPUTS & SENSORS ==================
void loop() {
  isConnected = Keyboard.isConnected(); 

  // 1. Handle Inputs
  if (isConnected) {
    handleModeSwitchNonBlocking();
    handleButtonsNonBlocking();
  }

  // 2. Sensor Logic (Only needed for Mouse or AI mode)
  bool sensorNeeded = (currentMode == MODE_MOUSE || currentMode == MODE_MEDIA_AI);

  if (sensorNeeded && millis() > last_sample_time + EI_SAMPLING_INTERVAL_MS) {
    last_sample_time = millis();

    sensors_event_t a, g, temp;
    
    // Lock I2C for sensor read
    if (xSemaphoreTake(i2cMutex, (TickType_t) 10) == pdTRUE) {
      mpu.getEvent(&a, &g, &temp);
      xSemaphoreGive(i2cMutex); 
    } else {
      return; 
    }

    if (isConnected) {
        if (currentMode == MODE_MOUSE) {
            Mouse.move(g.gyro.z * -SPEED, g.gyro.y * SPEED);
            feature_ix = 0; 
        } 
        else if (currentMode == MODE_MEDIA_AI) {
            if (feature_ix < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
                features[feature_ix++] = a.acceleration.x;
                features[feature_ix++] = a.acceleration.y;
                features[feature_ix++] = a.acceleration.z;
                if (feature_ix == EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) triggerInference = true; 
            }
        }
    }
  }
  
  delay(1); 
}

// ================== CORE 0: DISPLAY & AI & TIMER ==================
void core0Task(void * pvParameters) {
  static int lastMode = -1;
  static bool lastConn = !isConnected;
  static unsigned long lastScreenUpdate = 0;
  static unsigned long lastTimerCalc = 0;

  for(;;) {
    unsigned long currentMillis = millis();

    // 1. TIMER LOGIC (Runs independently of display refresh)
    if (currentMode == MODE_PRESENTATION && timerRunning) {
        if (currentMillis - lastTimerCalc >= 1000) { // Update every second
            if (timerRemaining >= 1000) {
                timerRemaining -= 1000;
            } else {
                timerRemaining = 0;
                timerRunning = false; // Stop when 0
            }
            lastTimerCalc = currentMillis;
        }
    }

    // 2. AI INFERENCE
    if (triggerInference && currentMode == MODE_MEDIA_AI) {
        runInference(); 
        feature_ix = 0; 
        triggerInference = false;
    }

    // 3. DISPLAY UPDATES
    // Force update if mode changed, connection changed, or timer needs refresh
    bool modeChanged = (currentMode != lastMode);
    bool connChanged = (isConnected != lastConn);
    bool timerUpdate = (currentMode == MODE_PRESENTATION && (currentMillis - lastScreenUpdate > 500));

    if (modeChanged || connChanged || timerUpdate) {
        
        // Update LED only on mode change
        if (modeChanged || connChanged) {
            setPixelColor(isConnected, currentMode);
        }
        
        // Update OLED
        if (xSemaphoreTake(i2cMutex, (TickType_t) 100) == pdTRUE) {
            updateDisplayLazy(isConnected, currentMode);
            xSemaphoreGive(i2cMutex);
        }
        
        lastMode = currentMode;
        lastConn = isConnected;
        lastScreenUpdate = currentMillis;
    }

    vTaskDelay(20 / portTICK_PERIOD_MS); 
  }
}

// ================== LOGIC HELPERS ==================

void handleButtonsNonBlocking() {
    unsigned long currentMillis = millis();
    int btnPressed = -1;

    // Read all buttons
    if (digitalRead(BTN1) == LOW) btnPressed = 1;
    else if (digitalRead(BTN2) == LOW) btnPressed = 2;
    else if (digitalRead(BTN3) == LOW) btnPressed = 3;
    else if (digitalRead(BTN4) == LOW) btnPressed = 4;

    if (btnPressed == -1) return; // No press

    // Debounce check
    if (currentMillis - lastBtnPress[btnPressed] < debounceDelay) return;
    lastBtnPress[btnPressed] = currentMillis;

    switch (currentMode) {
        case MODE_MOUSE:
            if (btnPressed == 1) Mouse.click(MOUSE_LEFT);
            if (btnPressed == 2) Mouse.click(MOUSE_RIGHT);
            if (btnPressed == 3) Mouse.move(0, 0, 1);
            if (btnPressed == 4) Mouse.move(0, 0, -1);
            break;

        case MODE_MEDIA:
        case MODE_MEDIA_AI:
            if (btnPressed == 1) Keyboard.write(KEY_MEDIA_PLAY_PAUSE);
            if (btnPressed == 2) Keyboard.write(KEY_MEDIA_NEXT_TRACK);
            if (btnPressed == 3) Keyboard.write(KEY_MEDIA_VOLUME_UP);
            if (btnPressed == 4) Keyboard.write(KEY_MEDIA_VOLUME_DOWN);
            break;

        case MODE_PRESENTATION:
            // BTN 1: Next Slide (Right Arrow)
            if (btnPressed == 1) {
                Keyboard.write(KEY_RIGHT_ARROW);
            }
            // BTN 2: Previous Slide (Left Arrow)
            if (btnPressed == 2) {
                Keyboard.write(KEY_LEFT_ARROW);
            }
            // BTN 3: Start / Pause Timer
            if (btnPressed == 3) {
                timerRunning = !timerRunning;
            }
            // BTN 4: Reset Timer
            if (btnPressed == 4) {
                timerRunning = false;
                timerRemaining = TIMER_START_VALUE;
            }
            break;
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
        if (isHolding) {
            if (millis() - pressStart > 50) { 
                currentMode = (currentMode + 1) % 4;
                
                // If entering Presentation Mode, reset timer logic slightly differently?
                // Currently keeping timer state unless user presses Reset (BTN4)
                // If you want auto-reset on mode entry, uncomment below:
                /* if (currentMode == MODE_PRESENTATION) {
                    timerRunning = false;
                    timerRemaining = TIMER_START_VALUE;
                }
                */
                
                feature_ix = 0; 
            }
            isHolding = false;
        }
    }
}

void updateDisplayLazy(bool connected, int mode) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(connected ? "BLE: ON" : "BLE: OFF");
    
    display.setCursor(0, 16);
    display.setTextSize(2);

    switch(mode) {
        case MODE_MOUSE:
            display.println("MOUSE");
            display.setTextSize(1);
            display.println("\nGyro Active");
            break;
        case MODE_MEDIA:
            display.println("MEDIA");
            display.setTextSize(1);
            display.println("\nBtns Only");
            break;
        case MODE_MEDIA_AI:
            display.println("AI CTRL");
            display.setTextSize(1);
            display.println("\nGestures ON");
            break;
        case MODE_PRESENTATION:
            display.println("SLIDES");
            
            // Format Time
            int minutes = timerRemaining / 60000;
            int seconds = (timerRemaining % 60000) / 1000;

            display.setTextSize(3);
            display.setCursor(10, 38);
            
            if (minutes < 10) display.print("0");
            display.print(minutes);
            display.print(":");
            if (seconds < 10) display.print("0");
            display.print(seconds);

            // Small Status Indicator
            display.setTextSize(1);
            display.setCursor(90, 0);
            display.print(timerRunning ? "RUN" : "PAUSE");
            break;
    }
    display.display();
}

void setPixelColor(bool connected, int mode) {
    if (!connected) {
        pixel.setPixelColor(0, pixel.Color(25, 0, 0)); // RED
    } else {
        switch(mode) {
            case MODE_MOUSE: pixel.setPixelColor(0, pixel.Color(0, 0, 50)); break; // Blue
            case MODE_MEDIA: pixel.setPixelColor(0, pixel.Color(0, 50, 0)); break; // Green
            case MODE_MEDIA_AI: pixel.setPixelColor(0, pixel.Color(30, 0, 30)); break; // Purple
            case MODE_PRESENTATION: pixel.setPixelColor(0, pixel.Color(50, 30, 0)); break; // Orange
        }
    }
    pixel.show();
}

// AI Function remains same
void runInference() {
    signal_t signal;
    ei_impulse_result_t result;
    int err = numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
    if (err != 0) return;
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, true);
    if (res != 0) return;

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > CONFIDENCE_THRESHOLD) {
            if (millis() - last_gesture_time > 1000) { 
                if (strcmp(result.classification[ix].label, "up") == 0) {
                    Keyboard.write(KEY_MEDIA_VOLUME_UP);
                    if (xSemaphoreTake(i2cMutex, 100)) {
                      displayAction("Vol UP");
                      xSemaphoreGive(i2cMutex);
                    }
                    last_gesture_time = millis();
                }
                else if (strcmp(result.classification[ix].label, "down") == 0) {
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
  if (r > 0) Serial.write(print_buf);
}
