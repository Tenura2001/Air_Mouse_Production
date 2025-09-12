/*
   ESP32 + MPU6050 + BLE Mouse + Buttons + RGB LEDs

   - Uses MPU6050 gyro values to move the BLE mouse pointer.
   - Buttons provide scroll up/down and mouse click.
   - RGB LEDs indicate Bluetooth connection and button press.

   Connections:
   - MPU6050 -> I2C (default ESP32 SDA=21, SCL=22)
   - Buttons -> GPIO pins defined below
   - RGB LEDs -> GPIO pins defined below
*/

#include <BleMouse.h>
#include <Adafruit_MPU6050.h>

// === RGB LED pins ===
#define G 15   // Green LED -> Bluetooth connected indicator
#define R 2    // Red LED -> Not connected indicator
#define B 4    // Blue LED -> Button pressed indicator

// === Button pins ===
#define UP_BTN   36   // Scroll up
#define DOWN_BTN 39   // Scroll down
#define LEFT_BTN 34   // Left click
//#define RIGHT_BTN 32 // Right click (optional if needed)

#define SPEED 10  // Mouse movement sensitivity

Adafruit_MPU6050 mpu;
BleMouse bleMouse;

bool sleepMPU = true;   // Keep MPU sleeping until BLE connects
bool status = true;     // To control initial LED blink

// === Setup ===
void setup() {
  Serial.begin(115200);

  // Pin modes for LEDs
  pinMode(R, OUTPUT);
  pinMode(G, OUTPUT);
  pinMode(B, OUTPUT);

  // Pin modes for buttons
  pinMode(UP_BTN, INPUT_PULLUP);
  pinMode(DOWN_BTN, INPUT_PULLUP);
  pinMode(LEFT_BTN, INPUT_PULLUP);
  // pinMode(RIGHT_BTN, INPUT_PULLUP); // enable if using right button

  // Start BLE mouse
  bleMouse.begin();

  // Initialize MPU6050
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);
    }
  }
  Serial.println("MPU6050 Found!");

  // Put MPU in sleep until BLE connects
  mpu.enableSleep(sleepMPU);
}

// === Main Loop ===
void loop() {
  if (bleMouse.isConnected()) {
    // Show connected with Green LED (only first time for 2s)
    if (status) {
      digitalWrite(G, HIGH);
      delay(2000);
      digitalWrite(G, LOW);
      status = false;
    }

    // Wake up MPU once connected
    if (sleepMPU) {
      delay(3000);
      Serial.println("MPU6050 awakened!");
      sleepMPU = false;
      mpu.enableSleep(sleepMPU);
      delay(500);
    }

    // Read MPU values
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // Mouse movement from gyro
    bleMouse.move(g.gyro.z * -SPEED, g.gyro.x * -SPEED);

    // Handle button inputs
    mouseControls();
  }
  else {
    // Not connected -> blink red LED
    notConnected();
  }
}

// === Functions ===

// Blink Red LED when BLE not connected
void notConnected() {
  digitalWrite(R, HIGH);
  delay(500);
  digitalWrite(R, LOW);
  delay(500);
}

// Small delay for smooth scrolling
void scrollDelay() {
  delay(50);
}

// Turn ON Blue LED while button pressed
void buttonPressed() {
  digitalWrite(B, HIGH);
}

// Handle mouse buttons and scroll
void mouseControls() {
  if (digitalRead(UP_BTN) == LOW) {  // Scroll up
    bleMouse.move(0, 0, 1);
    buttonPressed();
    scrollDelay();
  }
  else if (digitalRead(DOWN_BTN) == LOW) {  // Scroll down
    bleMouse.move(0, 0, -1);
    buttonPressed();
    scrollDelay();
  }
  else if (digitalRead(LEFT_BTN) == LOW) {  // Left click
    bleMouse.click(MOUSE_LEFT);
    buttonPressed();
    delay(250);
  }
  /*
  else if (digitalRead(RIGHT_BTN) == LOW) {  // Right click
    bleMouse.click(MOUSE_RIGHT);
    buttonPressed();
    delay(250);
  }
  */
  else {
    // No button pressed -> Blue LED OFF
    digitalWrite(B, LOW);
  }
}
