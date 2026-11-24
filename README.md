# Wearable IMU game controller: play a shooter by actually moving

**🏆 1st place, college project competition**

A toy gun and a body-worn IMU that turn real movement into game input. Walk in place and your character walks. Run and they sprint. Jump and they jump. Pull the trigger to shoot, tilt the gun to reload.

<!-- Add a photo / demo video here: ![demo](demo.gif) -->

## How it works

The ESP32 appears to the PC as a **Bluetooth keyboard + mouse (BLE HID)**, so no software runs on the computer. It works with any game.

```
MPU6050 (body) ──► activity + jump detection ──┐
Trigger / buttons / IR / pot (gun) ─────────────┼──► ESP32 ──BLE HID──► PC game
Joystick (strafe, TAB) ─────────────────────────┘
```

### Movement detection from raw accelerometer data
1. **Feature:** the magnitude of the change in acceleration between samples (jerk-like), so it doesn't depend on how the sensor is oriented on the body.
2. **Smoothing:** a 10-sample moving average of that feature.
3. **Classification:** thresholds for standing, walking and running, with **hysteresis** (different thresholds for going up and coming down) so the state doesn't flicker at the boundaries.
4. **Debouncing:** a new state must be confirmed for 3 consecutive samples and last at least 150 ms. A separate **quick-stop** path releases the keys immediately when movement stops, so the character doesn't keep running.
5. **Output:** walking holds `W`, running holds `W + Shift`, standing releases both.

### Jump detection: a state machine on acceleration magnitude
`IDLE → TAKEOFF (|a| > 18 m/s²) → AIR (|a| < 5, near free-fall) → LAND (|a| > 18) → press Space`, with an 800 ms cooldown. Requiring the full takeoff → free-fall → landing pattern means a single shock or step doesn't trigger a jump.

### Gun and other inputs
- Trigger → left click (rate-limited), aim button → holds right click, pickup → `F`
- IR sensor → reload (`R`), with 200 ms debouncing
- Potentiometer split into zones → weapon switching
- Joystick: center **calibrated at boot** (average of 50 samples), dead-zone of 400 counts, maps to W/A/S/D, with the joystick button mapped to TAB

## Problems I hit
- Raw thresholds on acceleration made walk/run flicker → added the moving average, hysteresis and confirmation counter.
- Running didn't stop when I stopped → added the quick-stop and hard-stop paths that release keys immediately.
- Jumps triggered on hard footsteps → replaced a single threshold with the takeoff/air/landing state machine and a cooldown.
- Joystick drift differed between boards → calibrate the center at boot instead of hard-coding it.

## Hardware
ESP32, MPU6050 (±8 g, ±500 °/s, 21 Hz on-chip low-pass filter), push buttons, IR obstacle sensor, potentiometer, analog joystick, toy gun.

## Files
- `imu_game_controller.ino`: final firmware (Arduino, `Adafruit_MPU6050`, `ESP32-BLE-Combo`)
