# ESP32-Tank: Modular, Network-Controlled DIY Tank

![Tests](https://github.com/SavageBeef/ESP32-Tank-PlatformIO/actions/workflows/unit_tests.yml/badge.svg)
![Build](https://github.com/SavageBeef/ESP32-Tank-PlatformIO/actions/workflows/firmware_build.yml/badge.svg)

An advanced implementation of the [ESP32-Tank](https://github.com/SavageBeef/ESP32-Tank) project, refactored into a **test-driven, modular C++ architecture** using the **PlatformIO** ecosystem.

---

## 🏗 Architecture Transformation

Originally developed as a single Arduino sketch (`.ino`), this project has been re-engineered to separate hardware-specific code from core control logic.

**Why the Transition?**
* **Decoupled Logic:** Movement calculations and battery math reside in standalone classes (`TankDrive`, `BatteryMath`), allowing the "brain" of the tank to be tested on a PC without ESP32 hardware.
* **Dependency Management:** Explicit library handling (Blynk, WebSerial etc.) ensures consistent, reproducible builds across different development environments.
* **CI/CD Readiness:** Structured to support **GitHub Actions**, ensuring that every commit is automatically validated against unit tests for movement logic and safety guards.

---

## 🛡 Key Features & Safety Guards

### 1. Refactored Movement Logic (`TankDrive.cpp`)
The movement engine is now a standalone module, enabling rigorous testing of the tank's behavior away from the physical chassis.
* **State-Machine Control:** Explicitly tracks `STATE_FWD`, `STATE_BWD`, `STATE_STOP`, `STATE_HALTED`, and `STATE_GUARD`.
* **Directional Lock Guard:** A hardware-safety feature that prevents motor drivers from instant polarity flips (e.g., Forward directly to Backward) without passing through a neutral zone, protecting the H-bridge and gearboxes.
* **Obstacle Intelligence:** Automated `STATE_HALTED` transition triggered by real-time ultrasonic sensor feedback.

### 2. Calibrated Battery Telemetry (`BatteryMath.h`)
Utilizes a voltage divider formula with custom calibration constants. Features automatic percentage clamping (0–100%) to provide reliable telemetry to the control interface.

---

## 🧪 Unit Testing & Quality Assurance

The project now includes a comprehensive test suite located in the `test/` directory using the **Unity** testing framework.
* **Native Testing:** Allows for "Logic-Only" testing. We simulate joystick coordinates (X, Y) on the host machine to verify expected PWM outputs and state transitions.
* **Regression Prevention:** Automated tests ensure that updates to dead-zones or speed mapping never compromise the **Illegal Transition Guard** or obstacle avoidance logic.
