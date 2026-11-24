#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <BleCombo.h>

// ============ MPU6050 SENSOR ============
Adafruit_MPU6050 mpu;

// ============ BLUETOOTH ============
BleComboKeyboard bleKeyboard("kneemazen", "ESP32", 100);
BleComboMouse bleMouse(&bleKeyboard);

// ============ GUN PINS ============
#define IR_SENSOR 33
#define BTN_SHOOT 32
#define BTN_PICKUP 27
#define BTN_RIGHT_CLICK 25
#define POT_PIN 34

// ============ JOYSTICK PINS ============
#define VRX_PIN 15  // Y-axis (SWAPPED)
#define VRY_PIN 35  // X-axis (SWAPPED)
#define SW_PIN 26
const int DEADZONE = 400;
int centerX = 0;
int centerY = 0;

// ============ JOYSTICK KEY STATES ============
bool joyWPressed = false;
bool joySPressed = false;
bool joyAPressed = false;
bool joyDPressed = false;
bool joyTabPressed = false;

// ============ POTENTIOMETER ============
#define POT_THRESHOLD 50
int lastPotValue = 0;
int potZone = 0;

// ============ IR SENSOR DEBOUNCE ============
#define IR_DEBOUNCE_TIME 200
unsigned long lastIRChangeTime = 0;
int lastIRState = HIGH;

// ============ ACTIVITY STATES ============
enum Activity {
  STANDING,
  WALKING,
  RUNNING,
  UNKNOWN
};

Activity currentActivity = UNKNOWN;
Activity lastConfirmedActivity = UNKNOWN;

// ============ THRESHOLDS ============
const float STANDING_THRESHOLD = 1.5;
const float WALKING_THRESHOLD = 5.0;
const float RUNNING_THRESHOLD = 10.0;

// ============ HYSTERESIS ============
const float STANDING_TO_WALKING = 2.5;
const float WALKING_TO_RUNNING = 8.0;
const float RUNNING_TO_WALKING = 7.0;
const float WALKING_TO_STANDING = 1.0;

// ============ PREVIOUS READINGS ============
float prevAccelX = 0, prevAccelY = 0, prevAccelZ = 0;
bool firstReading = true;

// ============ SMOOTHING BUFFER ============
const int SMOOTH_SIZE = 10;
float changeHistory[SMOOTH_SIZE];
int historyIndex = 0;

// ============ CONFIRMATION SYSTEM ============
const int CONFIRMATION_THRESHOLD = 3;
int confirmationCounter = 0;

// ============ TIMING ============
unsigned long lastActivityChange = 0;
const unsigned long MIN_ACTIVITY_DURATION = 150;

// ============ QUICK STOP ============
const float QUICK_STOP_THRESHOLD = 0.5;
const int QUICK_STOP_SAMPLES = 3;
int quickStopCounter = 0;

// ============ KEY STATES ============
bool wKeyPressed = false;
bool shiftKeyPressed = false;

// ============ JUMP DETECTION ============
enum JumpState {
  JUMP_IDLE,
  JUMP_TAKEOFF,
  JUMP_AIR,
  JUMP_LAND
};

JumpState jumpState = JUMP_IDLE;
unsigned long lastJumpTime = 0;
const unsigned long JUMP_COOLDOWN = 800;

// ============ GUN COOLDOWNS ============
const unsigned long ACTION_CD = 1000;
unsigned long lastScopeTime = 0;
unsigned long lastShootTime = 0;
unsigned long lastPickupTime = 0;
unsigned long lastRightClickTime = 0;
unsigned long lastPotTime = 0;

const unsigned long SHOOT_INTERVAL = 100;
const unsigned long PICKUP_INTERVAL = 300;
const unsigned long RIGHT_CLICK_INTERVAL = 50;
const unsigned long POT_INTERVAL = 50;

// ============ GUN STATES ============
bool isScoped = false;
bool isWalking = false;
bool isRunning = false;

// ============ RIGHT CLICK HOLD TRACKING ============
bool rightClickHeld = false;
int lastRightClickButtonState = HIGH;

// =============================================================
//                       JUMP DETECTION
// =============================================================
void detectJump(float mag) {
  unsigned long now = millis();

  if (now - lastJumpTime < JUMP_COOLDOWN) return;

  switch (jumpState) {

    case JUMP_IDLE:
      if (mag > 18) {
        jumpState = JUMP_TAKEOFF;
        Serial.println("Jump: TAKEOFF");
      }
      break;

    case JUMP_TAKEOFF:
      if (mag < 5) {
        jumpState = JUMP_AIR;
        Serial.println("Jump: AIR");
      }
      break;

    case JUMP_AIR:
      if (mag > 18) {
        jumpState = JUMP_LAND;
        Serial.println("Jump: LAND");
      }
      break;

    case JUMP_LAND:
      Serial.println("⭑⭑ JUMP TRIGGER → SPACE ⭑⭑");
      bleKeyboard.press(' ');
      delay(50);
      bleKeyboard.release(' ');
      lastJumpTime = now;
      jumpState = JUMP_IDLE;
      break;
  }
}

// =============================================================
//                KEYBOARD CONTROL FOR MOVEMENTS
// =============================================================
void handleActivityChange(Activity act) {

  if (act == lastConfirmedActivity) return;
  lastConfirmedActivity = act;

  if (act == STANDING) {
    if (wKeyPressed) bleKeyboard.release('w');
    if (shiftKeyPressed) bleKeyboard.release(KEY_LEFT_SHIFT);
    wKeyPressed = false;
    shiftKeyPressed = false;
    Serial.println("🧍 Standing: keys released");
    return;
  }

  if (act == WALKING) {
    Serial.println("🚶 Walking: HOLDING W");

    if (!wKeyPressed) {
      bleKeyboard.press('w');
      wKeyPressed = true;
      Serial.println("W key PRESSED");
    }

    if (shiftKeyPressed) {
      bleKeyboard.release(KEY_LEFT_SHIFT);
      shiftKeyPressed = false;
    }

    return;
  }

  if (act == RUNNING) {
    Serial.println("🏃 Running: HOLDING W + SHIFT");

    if (!wKeyPressed) {
      bleKeyboard.press('w');
      wKeyPressed = true;
      Serial.println("W key PRESSED");
    }

    if (!shiftKeyPressed) {
      bleKeyboard.press(KEY_LEFT_SHIFT);
      shiftKeyPressed = true;
      Serial.println("SHIFT key PRESSED");
    }

    return;
  }
}

// =============================================================
//                    JOYSTICK PROCESSING
// =============================================================
void processJoystick() {
  int xValue = analogRead(VRX_PIN);
  int yValue = analogRead(VRY_PIN);
  int button = digitalRead(SW_PIN);
  
  int xDiff = xValue - centerX;
  int yDiff = yValue - centerY;
  
  // Handle W (Forward) - xDiff > DEADZONE
  if (xDiff > DEADZONE) {
    if (!joyWPressed) {
      bleKeyboard.press('w');
      joyWPressed = true;
      Serial.println("JOY: W PRESSED");
    }
  } else {
    if (joyWPressed) {
      bleKeyboard.release('w');
      joyWPressed = false;
      Serial.println("JOY: W RELEASED");
    }
  }
  
  // Handle S (Backward) - xDiff < -DEADZONE
  if (xDiff < -DEADZONE) {
    if (!joySPressed) {
      bleKeyboard.press('s');
      joySPressed = true;
      Serial.println("JOY: S PRESSED");
    }
  } else {
    if (joySPressed) {
      bleKeyboard.release('s');
      joySPressed = false;
      Serial.println("JOY: S RELEASED");
    }
  }
  
  // Handle D (Right) - yDiff < -DEADZONE
  if (yDiff < -DEADZONE) {
    if (!joyDPressed) {
      bleKeyboard.press('d');
      joyDPressed = true;
      Serial.println("JOY: D PRESSED");
    }
  } else {
    if (joyDPressed) {
      bleKeyboard.release('d');
      joyDPressed = false;
      Serial.println("JOY: D RELEASED");
    }
  }
  
  // Handle A (Left) - yDiff > DEADZONE
  if (yDiff > DEADZONE) {
    if (!joyAPressed) {
      bleKeyboard.press('a');
      joyAPressed = true;
      Serial.println("JOY: A PRESSED");
    }
  } else {
    if (joyAPressed) {
      bleKeyboard.release('a');
      joyAPressed = false;
      Serial.println("JOY: A RELEASED");
    }
  }
  
  // Handle Joystick Button (TAB)
  if (button == LOW) {
    if (!joyTabPressed) {
      bleKeyboard.press(KEY_TAB);
      joyTabPressed = true;
      Serial.println("JOY: TAB PRESSED");
    }
  } else {
    if (joyTabPressed) {
      bleKeyboard.release(KEY_TAB);
      joyTabPressed = false;
      Serial.println("JOY: TAB RELEASED");
    }
  }
}

// =============================================================
//                    GUN PROCESSING
// =============================================================
void processGun(unsigned long now) {
  if (now - lastShootTime >= SHOOT_INTERVAL) {
    if (digitalRead(BTN_SHOOT) == LOW) {
      bleMouse.click(MOUSE_LEFT);
      Serial.println("SHOOT!");
      lastShootTime = now;
    }
  }

  if (now - lastPickupTime >= PICKUP_INTERVAL) {
    if (digitalRead(BTN_PICKUP) == LOW) {
      bleKeyboard.write('f');
      Serial.println("PICKUP!");
      lastPickupTime = now;
    }
  }

  int ir = digitalRead(IR_SENSOR);
  if (ir != lastIRState && (now - lastIRChangeTime) >= IR_DEBOUNCE_TIME) {
    if (ir == LOW) {
      bleKeyboard.write('r');
      Serial.println("RELOAD!");
    }
    lastIRState = ir;
    lastIRChangeTime = now;
  }
}

// =============================================================
//                    RIGHT CLICK BUTTON (HOLD)
// =============================================================
void processRightClick(unsigned long now) {
  int buttonState = digitalRead(BTN_RIGHT_CLICK);
  
  // Detect state change (debounce with reading)
  if (buttonState != lastRightClickButtonState) {
    delay(10); // Small debounce
    buttonState = digitalRead(BTN_RIGHT_CLICK); // Re-read to confirm
    lastRightClickButtonState = buttonState;
    
    // Button pressed (LOW) - START HOLDING
    if (buttonState == LOW && !rightClickHeld) {
      bleMouse.press(MOUSE_RIGHT);
      rightClickHeld = true;
      Serial.println("🖱️ RIGHT CLICK: HOLDING");
    } 
    // Button released (HIGH) - STOP HOLDING
    else if (buttonState == HIGH && rightClickHeld) {
      bleMouse.release(MOUSE_RIGHT);
      rightClickHeld = false;
      Serial.println("🖱️ RIGHT CLICK: RELEASED");
    }
  }
}

// =============================================================
//                    POTENTIOMETER PROCESSING
// =============================================================
void processPotentiometer(unsigned long now) {
  if (now - lastPotTime >= POT_INTERVAL) {
    int rawValue = analogRead(POT_PIN);

    int newZone;
    if (rawValue < 1365) {
      newZone = 0;
    } else if (rawValue >= 1365 && rawValue < 2730) {
      newZone = 1;
    } else {
      newZone = 2;
    }

    if (abs(rawValue - lastPotValue) > POT_THRESHOLD) {
      if (newZone != potZone) {
        if (newZone == 0) {
          bleKeyboard.write('0');
          Serial.println("POT: START (0)");
        } else if (newZone == 2) {
          bleKeyboard.write('5');
          Serial.println("POT: END (5)");
        }
        potZone = newZone;
      }
      lastPotValue = rawValue;
    }
    lastPotTime = now;
  }
}

// =============================================================
//                    HELPER FUNCTION
// =============================================================
String activityToString(Activity act) {
  switch(act) {
    case STANDING: return "STANDING";
    case WALKING: return "WALKING";
    case RUNNING: return "RUNNING";
    case UNKNOWN: return "UNKNOWN";
    default: return "???";
  }
}

// =============================================================
//                         SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    while (1) delay(10);
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  for (int i = 0; i < SMOOTH_SIZE; i++) changeHistory[i] = 0;

  // -------- JOYSTICK CALIBRATION --------
  pinMode(SW_PIN, INPUT_PULLUP);
  Serial.println("Calibrating joystick center...");
  long sumX = 0, sumY = 0;
  for (int i = 0; i < 50; i++) {
    sumX += analogRead(VRX_PIN);
    sumY += analogRead(VRY_PIN);
    delay(20);
  }
  centerX = sumX / 50;
  centerY = sumY / 50;
  Serial.print("Joystick Center: X=");
  Serial.print(centerX);
  Serial.print(" Y=");
  Serial.println(centerY);

  bleKeyboard.begin();
  bleMouse.begin();
  Serial.println("BLE ready.");

  pinMode(IR_SENSOR, INPUT);
  pinMode(BTN_SHOOT, INPUT_PULLUP);
  pinMode(BTN_PICKUP, INPUT_PULLUP);
  pinMode(BTN_RIGHT_CLICK, INPUT_PULLUP);
  pinMode(POT_PIN, INPUT);

  Serial.println("=== PUBG CONTROLLER READY ===");
  delay(1000);
}

// =============================================================
//                       MAIN LOOP
// =============================================================
void loop() {
  unsigned long now = millis();

  if (!bleKeyboard.isConnected()) {
    Serial.println("Waiting for BLE...");
    delay(1000);
    return;
  }

  // -------- JOYSTICK INPUT --------
  processJoystick();

  // -------- MPU6050: Movement Detection --------
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float accelX = a.acceleration.x;
  float accelY = a.acceleration.y;
  float accelZ = a.acceleration.z;

  float mag = sqrt(accelX*accelX + accelY*accelY + accelZ*accelZ);
  detectJump(mag);

  float rateOfChange = 0;

  if (!firstReading) {
    float dx = abs(accelX - prevAccelX);
    float dy = abs(accelY - prevAccelY);
    float dz = abs(accelZ - prevAccelZ);
    rateOfChange = sqrt(dx*dx + dy*dy + dz*dz);
  } else {
    firstReading = false;
  }

  prevAccelX = accelX;
  prevAccelY = accelY;
  prevAccelZ = accelZ;

  changeHistory[historyIndex] = rateOfChange;
  historyIndex = (historyIndex + 1) % SMOOTH_SIZE;

  float avgChange = 0;
  for (int i = 0; i < SMOOTH_SIZE; i++) avgChange += changeHistory[i];
  avgChange /= SMOOTH_SIZE;

  if (avgChange < 0.8) {
    if (wKeyPressed) {
      bleKeyboard.release('w');
      wKeyPressed = false;
    }
    if (shiftKeyPressed) {
      bleKeyboard.release(KEY_LEFT_SHIFT);
      shiftKeyPressed = false;
    }

    currentActivity = STANDING;
    lastConfirmedActivity = STANDING;
    quickStopCounter = 0;
    confirmationCounter = 0;

    Serial.println("🛑 HARD STOP: All keys released");
    delay(80);
    
    // Continue to gun processing even when standing
    processGun(now);
    processRightClick(now);
    processPotentiometer(now);
    return;
  }

  if (avgChange < QUICK_STOP_THRESHOLD &&
      (currentActivity == WALKING || currentActivity == RUNNING)) {
    quickStopCounter++;
    if (quickStopCounter >= QUICK_STOP_SAMPLES) {
      currentActivity = STANDING;
      handleActivityChange(STANDING);
      quickStopCounter = 0;
      confirmationCounter = 0;
      lastActivityChange = millis();
    }
  } else {
    quickStopCounter = 0;
  }

  Activity newActivity = UNKNOWN;

  if (avgChange < STANDING_THRESHOLD)
    newActivity = STANDING;
  else if (avgChange < WALKING_THRESHOLD)
    newActivity = WALKING;
  else if (avgChange >= RUNNING_THRESHOLD)
    newActivity = RUNNING;
  else
    newActivity = WALKING;

  if (currentActivity != UNKNOWN) {
    if (currentActivity == STANDING && newActivity != STANDING)
      if (avgChange < STANDING_TO_WALKING) newActivity = STANDING;

    if (currentActivity == WALKING && newActivity == RUNNING)
      if (avgChange < WALKING_TO_RUNNING) newActivity = WALKING;

    if (currentActivity == RUNNING && newActivity == WALKING)
      if (avgChange > RUNNING_TO_WALKING) newActivity = RUNNING;

    if (currentActivity == WALKING && newActivity == STANDING)
      if (avgChange > WALKING_TO_STANDING) newActivity = WALKING;
  }

  if (newActivity == currentActivity) {
    confirmationCounter = 0;
  } else {
    confirmationCounter++;
  }

  if (confirmationCounter >= CONFIRMATION_THRESHOLD &&
      (now - lastActivityChange >= MIN_ACTIVITY_DURATION)) {

    if (newActivity != currentActivity) {
      currentActivity = newActivity;
      lastActivityChange = now;
      confirmationCounter = 0;

      handleActivityChange(currentActivity);
    }
  }

  // -------- GUN: Scope + Shoot + Pickup + Reload --------
  processGun(now);

  // -------- RIGHT CLICK BUTTON (HOLD) --------
  processRightClick(now);

  // -------- POTENTIOMETER --------
  processPotentiometer(now);

  delay(50);
}