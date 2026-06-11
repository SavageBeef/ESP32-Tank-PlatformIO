/******************************************************************************
  ESP32 WiFi Tank Firmware
  
  Architected for non-blocking asynchronous execution, low-power software standby,
  and dynamic high-torque motor control wrappers.
 ******************************************************************************/

// ============================================================================
// 1. PREPROCESSOR DIRECTIVES & LIBRARY INCLUDES
// ============================================================================
#include <Arduino.h>

#define BLYNK_PRINT Serial        
#include <BlynkSimpleEsp32.h>
#include <SR04.h>                 // Ultrasonic Sensor driver library

// Custom System Control Submodules
#include "TankDrive.h"            // Tank Core Logic
#include "BatteryMath.h"          // Abstracted Battery voltage calculations
#include "NetworkHelper.h"        // WIFI, OTA, WebSerial, Captive Portals
#include "WebSerial_printf.h"     // Printf wrapper for the browser console
#include "dual_printf.h"          // Consolidated terminal printing routing

// ============================================================================
// 2. PHYSICAL HARDWARE PIN MAPS (#define IO Architecture)
// ============================================================================
#define LED                 2     // Onboard status indicator

// Motor Driver Channel 1 (Left Drive Train)
#define motorL_EN           10    // PWM speed channel pin
#define motorL_Negative     11    // Phase orientation control line
#define motorL_Positive     12    // Phase orientation control line

// Motor Driver Channel 2 (Right Drive Train)
#define motorR_EN           16    // PWM speed channel pin
#define motorR_Positive     17    // Phase orientation control line
#define motorR_Negative     18    // Phase orientation control line

// System Telemetry & Transistor Control Rails
#define batteryVoltagePin   5     // ADC input tracking input potential
#define TRIG_PIN            41    // Sonic trigger pulse line
#define ECHO_PIN            42    // Sonic echo timing line
#define lights              47    // Gate drive signal to Headlight NPN1 Transistor
#define currentToUSonic     48    // Gate drive signal to Ultrasonic NPN2 Transistor

// ============================================================================
// 3. HARDWARE PERIPHERAL CONFIGURATIONS (Immutable Flash Assets)
// ============================================================================
// PWM Engine Frequency and Depth Bounds
const uint32_t freq       = 15000;  // 15 kHz inductive tuning for ComXim 25GA370 motors
const uint16_t lightsFreq = 1000;   // 1 kHz flicker-free baseline for headlight dimming
const uint8_t  resolution = 10;     // 10-bit deep duty resolution canvas (0-1023)

// Software Standby💤 Constraints
#define STANDBY_BUTTON_VPIN 17      // Blynk App virtual standby switch map
const uint16_t SLEEP_DURATION_SEC = 900; // Standby interval (15 minutes)

// Resistor Divider Networks & Telemetry Scales
const float maxBatteryVoltage  = 16.68; // Fully charged cell ceiling target (4.17V/cell)
const float minBatteryVoltage  = 12.00; // System cutoff floor limit (3.0V/cell)
const float R1                 = 41860.0; // Multimeter audited high-side resistor value - R1 in voltage divider (42kΩ)
const float R2                 = 9989.0;  // Multimeter audited low-side resistor value - R2 in voltage divider (10kΩ)
const float calibrationFactor  = 1.025;   // Empirical ADC offset calibration coefficient = Voltage reading from multimeter / Voltage after voltage divider

// ============================================================================
// 4. INSTANTIATED LIBRARY OBJECTS & APP UI LINKS
// ============================================================================
NetworkHelper network;              // Abstracted network and credential service
BlynkTimer    timer;                // Non-blocking background callback scheduler
SR04          sr04 = SR04(ECHO_PIN, TRIG_PIN); // Sonic sensor instantiation
WidgetLED     ledIndicator(V11);    // Virtual Dashboard diagnostic dashboard link

// ============================================================================
// 5. RUNTIME STATE VARIABLES & SYSTEM FLAGS (Dynamic SRAM)
// ============================================================================
// Core Speed Parameters
uint16_t minSpeed   = 750;          // Dynamic operational floor (auto-scaled)
uint16_t maxSpeed   = 1023;         // Dynamic execution limit (slider adjusted)
const uint8_t noSpeed = 0;          // Structural reference for absolute halt
XRegion lastXRegion = X_NEUTRAL;    // Persistent regional tracking for anti-shear safety

// Sensor Outputs & Limit Values
uint16_t distance;                  // Variable tracking calculated distance (cm)
bool     uSonicState      = false;  // Virtual slider toggle tracking switch
bool     isHaltedByUSonic = false;  // Intercept flag for structural obstacle braking
uint8_t  ultrasonicLimit  = 40;     // Minimum obstacle proximity buffer limit (cm)

// Sleep Trigger
bool     isStandbyActive  = false;  // Tracking standby loop activation state

// ============================================================================
// 6. GLOBAL TEMPLATE FUNCTIONS & PROTOTYPES
// ============================================================================
// Consolidated Cross-Terminal Mirroring Templates
template <typename T>
void dualPrint(T data) {
  Serial.print(data);
  WebSerial.print(data);
}

template <typename T>
void dualPrintln(T data) {
  Serial.println(data);
  WebSerial.println(data);
}

// Internal Compilation Forward Declarations
void applyToMotors(TankCommand cmd);
void uSonicButtonCheck();
void readBatteryVoltage();
void putTankToSoftwareSleep();

void setup()
{
  Serial.begin(115200);
  delay(2000); // Allow hardware lines and serial interface to settle
  
  Serial.println("\n\n=== TANK BOOTING ===");
  Serial.println("Initializing systems...");

  // Configures WiFi, access points, OTA endpoints, and mounts local servers
  network.begin("SavageTank", "password123"); // Captive portal AP prefix name and password

  // --- ATTEMPT BLYNK CONNECTION ---
  Serial.println("\n--- Blynk Connection Phase ---");
  int port_int = atoi(blynk_port); // Convert port char array to integer for the Blynk.config()function

  Serial.print("Attempting Blynk Server: ");
  Serial.print(blynk_server);
  Serial.print(":");
  Serial.println(port_int);

  Blynk.config(blynk_auth, blynk_server, port_int);

  // Synchronous connection attempt loop
  int blynk_startup_attempts = 0;
  while (!Blynk.connected() && blynk_startup_attempts < 5) {
    Serial.print(".");
    Blynk.connect(4000); // 4-second processing allotment per iteration
    blynk_startup_attempts++;
  }

  // Fallback to internal captive credential provisioner on validation breakdown
  if (!Blynk.connected()) {
    Serial.println("\nBlynk server connection failed. Launching Blynk Provisioner...");
    network.launchBlynkProvisioner();

    port_int = atoi(blynk_port);
    Blynk.config(blynk_auth, blynk_server, port_int);

    blynk_startup_attempts = 0;
    while (!Blynk.connected() && blynk_startup_attempts < 5) {
      Serial.print(".");
      Blynk.connect(4000);
      blynk_startup_attempts++;
    }

    if (!Blynk.connected()) {
      Serial.println("\nBlynk provisioning failed. Restarting...");
      delay(3000);
      ESP.restart();
    } 
  }

  Serial.println("\nBlynk connected successfully!");

  // Pin Configuration Declarations
  pinMode(LED, OUTPUT);
  pinMode(motorL_Negative, OUTPUT);
  pinMode(motorL_Positive, OUTPUT);
  pinMode(motorL_EN, OUTPUT);
  pinMode(motorR_Negative, OUTPUT);
  pinMode(motorR_Positive, OUTPUT);
  pinMode(motorR_EN, OUTPUT);
  pinMode(lights, OUTPUT);
  pinMode(currentToUSonic, OUTPUT);
  pinMode(batteryVoltagePin, INPUT);
  
  // Link hardware peripheral channels to assigned outputs via LEDC layer
  ledcAttach(motorL_EN, freq, resolution);
  ledcAttach(motorR_EN, freq, resolution);
  ledcAttach(lights, lightsFreq, resolution);

  // Initialization Diagnostic Light Pattern
  ledcWrite(lights, 0);   delay(250);
  ledcWrite(lights, 511); delay(250);
  ledcWrite(lights, 0);   delay(250);
  ledcWrite(lights, 766); delay(250);
  ledcWrite(lights, 0);

  // Core Asynchronous Polling Interval Schedules
  // Set a function to be called every X ms
  timer.setInterval(50L, uSonicButtonCheck); 
  timer.setInterval(1000L, readBatteryVoltage);

  // Publish baseline constraint setting back up to dashboard engine
  Blynk.virtualWrite(V8, ultrasonicLimit); 
}

void loop()
{
  // --- 1. ABSOLUTE TOP STANDBY SAFETY GATE ---
  // If the tank is in sleep mode, bypass ALL core processing, driving mechanics, 
  // roaming checks, and telemetry instantly before any other logic cycles.
  if (isStandbyActive) {
    putTankToSoftwareSleep();
    return; // Stop right here and hand control back to the execution line
  }

  // --- 2. CORE ENGINE RUNNERS ---
  // Must run freely out in the open loop so local timers (like battery math 
  // and ultrasonic monitoring) keep protecting the hardware even if Wi-Fi drops.
  Blynk.run(); 
  timer.run(); 

  // --- 3. IMMEDIATE SAFETY FAILSAFE ---
  // Failsafe to halt the tank when connection is lost mid-run.
  if (WiFi.status() != WL_CONNECTED || !Blynk.connected()) {
    ledcWrite(motorL_EN, noSpeed);
    ledcWrite(motorR_EN, noSpeed);
  }

  // --- 4. BACKGROUND UTILITIES ---
  network.handle(); // Manages OTA updates and background network tasks

  // --- 5. ACCESS POINT ROAMING & RECONNECT MANAGER ---
  static unsigned long lastWiFiCheck = 0;
  const unsigned long wifiCheckInterval = 10000; 

  if (millis() - lastWiFiCheck > wifiCheckInterval) {
    lastWiFiCheck = millis();

    if (WiFi.status() != WL_CONNECTED) {
      dualPrintln("WiFi connection lost! Initiating background reconnect...");
      WiFi.begin(); 
    } 
    else if (WiFi.status() == WL_CONNECTED) {
      int rssi = WiFi.RSSI();
      // -78 dBm captures the weak zone right before packet loss and input lag begin
      if (rssi < -78) {
        dualPrintf("Signal too weak (%d dBm). Disconnecting to hunt for closer AP...\n", rssi);
        ledcWrite(motorL_EN, noSpeed);
        ledcWrite(motorR_EN, noSpeed);
        WiFi.disconnect(false); 
        WiFi.begin();           
      }
    }
  }

  // --- 6. NON-BLOCKING LED STATUS STATE MACHINE ---
  // We use a single, fast-running clock check to handle all visual modes dynamically
  static unsigned long lastLEDToggle = 0;
  unsigned long ledInterval = 0; // 0 means everything is healthy (LED OFF)

  if (WiFi.status() != WL_CONNECTED) {
    ledInterval = 100; // 🛑 Rapid strobe: Critical Wi-Fi loss/AP hunt
  } 
  else if (!Blynk.connected()) {
    ledInterval = 500; // ⚠️ Steady blink: Wi-Fi good, but Blynk server dropped
  } 
  else {
    ledInterval = 0;   //  OFF: Everything is perfectly healthy
  }

  // Execute the non-blocking blink logic based on the active state interval
  if (ledInterval == 0) {
    digitalWrite(LED, LOW); // Turn the LED completely off during normal operation
  } else {
    if (millis() - lastLEDToggle >= ledInterval) {
      lastLEDToggle = millis();
      digitalWrite(LED, !digitalRead(LED)); // Toggle the LED state cleanly
    }
  }

  // --- 7. BACKGROUND TELEMETRY & PROVISIONING ---
  if (WiFi.status() == WL_CONNECTED) {
    // Send Wifi signal strength to virtual pin for in-app visual tracking
    static unsigned long lastSignalCheck = 0;
    if (millis() - lastSignalCheck > 5000) { // Every 5 Seconds
      lastSignalCheck = millis();
      Blynk.virtualWrite(V16, WiFi.RSSI());
    }

    // Background Provisioner Trigger Check
    if (!Blynk.connected()) {
      static unsigned long lastAttemptTime = 0;
      if (millis() - lastAttemptTime > 10000) { // Every 10 Seconds
        lastAttemptTime = millis();
        blynk_connection_attempts++;
        dualPrintln("Blynk connecting in background...");
        // If we've failed too many times and not already provisioning, launch provisioner
        if (blynk_connection_attempts >= BLYNK_MAX_ATTEMPTS && !has_provisioned_blynk) {
          dualPrintln("Blynk connection failed multiple times. Launching Blynk Provisioner...");
          has_provisioned_blynk = true; // Prevent repeated provisioning attempts
          network.launchBlynkProvisioner();
          // Reconfigure and try again
          int port_int = atoi(blynk_port);
          Blynk.config(blynk_auth, blynk_server, port_int);
          blynk_connection_attempts = 0; // Reset counter 
        }
      }
    }
    else {
      blynk_connection_attempts = 0; // Reset on successful connection
    }
  }
}

BLYNK_CONNECTED() {
  if (isStandbyActive) {
    dualPrintln("🔄 Background Check-in: Syncing Standby Pin (V17) only...");
    Blynk.syncVirtual(STANDBY_BUTTON_VPIN); // Pulls ONLY the standby switch state to minimize data/power usage
  } else {
    dualPrintln("🔄 Fresh Wake/Boot: Syncing all dashboard widgets...");
    Blynk.syncAll(); // Pulls all stored widget states from Blynk Cloud to update hardware variables
  }
}

// Joystick control
BLYNK_WRITE(V5)
{
  int x = param[0].asInt();
  int y = param[1].asInt();

  // 1. EXECUTE VERIFIED LOGIC
  // We pass our current pins/state into the class
  // Parameters: x, y, isBlocked, lastXRegion, maxSpeed, minSpeed, noSpeed
  TankCommand cmd = TankDrive::calculate(x, y, isHaltedByUSonic, lastXRegion, maxSpeed, minSpeed, noSpeed);

  // DIRECTIONAL LOCK GUARD
  // If the TankDrive says we are in a Guard state, we exit early.
  // This keeps the motors running at their current speed.
  if (cmd.state == STATE_GUARD) {
    return; 
  }

  // 2. COMMIT STATE
  // Update the persistent state for the next joystick movement
  lastXRegion = cmd.nextXRegion;

  // 3. APPLY TO HARDWARE
  // One central place to handle motor pins
  applyToMotors(cmd);

  dualPrintf("L-PWM: %d | R-PWM: %d\n", cmd.leftPWM, cmd.rightPWM);
}

void applyToMotors(TankCommand cmd){
  // Set Directional Pins
  digitalWrite(motorL_Positive, cmd.motorL_Pos);
  digitalWrite(motorL_Negative, cmd.motorL_Neg);
  digitalWrite(motorR_Positive, cmd.motorR_Pos);
  digitalWrite(motorR_Negative, cmd.motorR_Neg);

  // Set Speed via PWM
  ledcWrite(motorL_EN, cmd.leftPWM);
  ledcWrite(motorR_EN, cmd.rightPWM);
}

// Speed slider
BLYNK_WRITE(V6) 
{
  int pinData = param.asInt(); // Blynk slider is 0-1023

  // Calculate the ceiling based on resolution
  int pwmMax = (1 << resolution) - 1; 

  // Calculate the floor (73.3% of max)
  // We use 0.733 to match 750/1023 ratio
  int floorSpeed = 0.733 * pwmMax;

  if (pinData <= 0) { // Limiter
    maxSpeed = 0;
    Blynk.virtualWrite(V6, maxSpeed); 
  }
  else if (pinData > pwmMax){ // Limiter
    maxSpeed = pwmMax;
    Blynk.virtualWrite(V6, maxSpeed);
  } 
  else {
    // Map the 0-1023 slider range to the dynamic hardware range
    maxSpeed = map(pinData, 1, 1023, floorSpeed, pwmMax);
  }

  // Calculate turn speed (75% of current max), dynamically scale with every resolution
  minSpeed = 0.75 * maxSpeed;

  dualPrintf("Res: %d-bit | Max: %d | Min: %d\n", resolution, maxSpeed, minSpeed);
}

// Ultrasonic Sensor button
BLYNK_WRITE(V3) 
{
  uSonicState = param.asInt(); 
}

// Function to check ultrasonic button state. On/Off.
void uSonicButtonCheck() 
{
  // Safety Gate: If we are just checking in, keep the sensor powered off and skip!
  if (isStandbyActive) {
    digitalWrite(currentToUSonic, LOW);
    return;
  }

  if (uSonicState == HIGH) // If button is on.
  {
    // Turns on the NPN Transistor to supply power to the sensor
    digitalWrite(currentToUSonic, HIGH); 
    
    distance = sr04.Distance(); // Measure distance
    Blynk.virtualWrite(V7, distance); // Display distance in app
    
    // Update the Global Flag
    // This is the ONLY thing the TankDrive (The Brain) needs to know to halt the tank
    isHaltedByUSonic = (distance <= ultrasonicLimit);

    if (isHaltedByUSonic) {
        // Visual feedback for the Blynk app
        Blynk.virtualWrite(V5, 512, 512); 
    }    
  }
  else {
    digitalWrite(currentToUSonic, LOW); // Cut power to NPN Transistor
    Blynk.virtualWrite(V7, "--"); 
    isHaltedByUSonic = false; // Always clear the flag when the sensor is turned OFF
  } 
}

// Numeric Input to set Ultrasonic Limit.
BLYNK_WRITE(V8) {
  // Clamp the user input to a safe operational range (20cm to 200cm)
  ultrasonicLimit = constrain(param.asInt(), 20, 200);
  Blynk.virtualWrite(V8, ultrasonicLimit);
}

// Led lights slider
BLYNK_WRITE(V4) 
{
  int pinData = param.asInt(); // Blynk slider is 0-1023

  // Calculate the ceiling based on resolution
  int pwmMax = (1 << resolution) - 1; 
  // One-line safety clamp
  int brightness = constrain(pinData, 0, pwmMax);
  
  ledcWrite(lights, brightness);
  
  // Update the WidgetLED (0-255 scale)
  ledIndicator.setValue((float(brightness) / pwmMax) * 255);

  // Sync UI if the slider went out of bounds
  if(pinData != brightness) Blynk.virtualWrite(V4, brightness);
}

// Battery Voltage
void readBatteryVoltage() {
  float sum = 0;
  const int numSamples = 64;

  // Hardware Read (Stability Loop)
  for (int i = 0; i < numSamples; i++) {
    sum += analogReadMilliVolts(batteryVoltagePin);
    delay(5); 
  }
  float averageMilliVolts = sum / numSamples;

  // Execute Abstracted Math
  float currentVoltage = BatteryMath::calculateVoltage(averageMilliVolts, R1, R2, calibrationFactor);
  float batteryPct = BatteryMath::calculatePercentage(currentVoltage, minBatteryVoltage, maxBatteryVoltage);

  // Blynk/UI Reporting
  Blynk.virtualWrite(V14, currentVoltage); // Value Display
  Blynk.virtualWrite(V15, batteryPct);    // Gauge
  
  // Optional: Log to terminal for auditing
  // dualPrintf("Battery: %.2fV (%d%%)\n", currentVoltage, (int)batteryPct);
}

// Handles incoming data from the Standby Switch on V17
BLYNK_WRITE(STANDBY_BUTTON_VPIN) {
  int standbyState = param.asInt();
  
  if (standbyState == HIGH) {
    // Switch is ON (User wants the tank asleep)
    dualPrintln("🔒 Standby switch is ACTIVE. Preparing for software sleep...");
    
    // CRITICAL FIX: Remove the direct call to putTankToSoftwareSleep()
    // This avoids a recursive reentrancy loop where the ESP32 goes to sleep
    // from inside an active Blynk callback. Instead, we flip this flag
    // and let the main loop() safety gate handle the transition cleanly.
    isStandbyActive = true;
  } 
  else {
    // Switch was turned OFF (User wants to drive!)
    dualPrintln("🔓 Standby switch DEACTIVATED. Tank is awake and ready to roll!");
    
    // Ensure direction pins are clean and ready
    digitalWrite(motorL_Positive, LOW);
    digitalWrite(motorL_Negative, LOW);
    digitalWrite(motorR_Positive, LOW);
    digitalWrite(motorR_Negative, LOW);

    // We are no longer checking in; we are ready to roll!
    isStandbyActive = false;
  }
}

void putTankToSoftwareSleep() {
  dualPrintln("STANDBY: Initializing Software Sleep Mode...");

  // 1. CRITICAL MOTOR SAFETY FENCE
  // Force all PWM speeds to zero instantly before breaking control paths
  ledcWrite(motorL_EN, noSpeed);
  ledcWrite(motorR_EN, noSpeed);

  // 2. HEADLIGHTS & ULTRASONIC HARDWARE LOCKDOWN
  ledcWrite(lights, 0);                  // Force driving headlights completely OFF
  digitalWrite(currentToUSonic, LOW);    // Open NPN transistor, cutting hardware power to HC-SR04

  // 3. MOTOR DRIVER ISOLATION (Prevents Phantom Power Leakage)
  // Shifting control pins to INPUT breaks the digital circuit path to the L293D.
  // This prevents current from leaking from the ESP32 GPIOs into a non-powered driver.
  pinMode(motorL_EN, INPUT);
  pinMode(motorR_EN, INPUT);
  pinMode(motorL_Positive, INPUT);
  pinMode(motorL_Negative, INPUT);
  pinMode(motorR_Positive, INPUT);
  pinMode(motorR_Negative, INPUT);

  // 4. RADIO & PERIPHERAL SHUTDOWN
  Blynk.disconnect();
  WiFi.disconnect(true);    // 'true' turns off the internal RF radio circuitry completely
  digitalWrite(LED, LOW);   // Turn off diagnostic LED to save power

  dualPrintf("💤 Entering Light Sleep for %llu seconds. RAM preserved.\n", SLEEP_DURATION_SEC);
  delay(10); // Give Serial buffer a millisecond to flush the print statement

  // 5. CONFIGURE WAKEUP SOURCE
  // Convert seconds to microseconds (ULL forces Unsigned Long Long math)
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_SEC * 1000000ULL);

  // 6. ENTER LIGHT SLEEP MODE
  // The ESP32 freezes execution right here. CPU clock stops.
  esp_light_sleep_start();

  // ==================================================================
  // WAKEUP / RECOVERY SEQUENCE (Runs automatically when timer fires)
  // ==================================================================
  
  // 1. RE-ESTABLISH DIGITAL WALLS (Restore GPIO modes)
  pinMode(motorL_Positive, OUTPUT);
  pinMode(motorL_Negative, OUTPUT);
  pinMode(motorR_Positive, OUTPUT);
  pinMode(motorR_Negative, OUTPUT);

  // ledcAttach handles the output configuration and peripheral routing automatically.
  ledcAttach(motorL_EN, freq, resolution);
  ledcAttach(motorR_EN, freq, resolution);
  ledcAttach(lights, lightsFreq, resolution);

  // Keep currentToUSonic as OUTPUT but ensure it stays LOW for now
  pinMode(currentToUSonic, OUTPUT);
  digitalWrite(currentToUSonic, LOW);

  // 2. FLAG THIS AS A BACKGROUND CHECK-IN
  // Maintain standby mode state as true during the sync sequence
  isStandbyActive = true;

  // 3. PHASE 1: WAKE UP RF RADIO & CONNECT TO WI-FI FIRST
  // Evaluates your SLEEP_DURATION_SEC variable to format the console log cleanly
  if (SLEEP_DURATION_SEC % 60 == 0) {
    dualPrintf("☀️ %llu-Minute Check-in: Waking up RF radio and searching for network...\n", SLEEP_DURATION_SEC / 60);
  } else {
    dualPrintf("☀️ %llu-Second Check-in: Waking up RF radio and searching for network...\n", SLEEP_DURATION_SEC);
  }

  WiFi.mode(WIFI_STA); // Turn the RF radio back on from the WIFI_OFF state
  WiFi.begin(); // Uses the credentials safely cached in the ESP32's NVS flash slot

  unsigned long wifiStart = millis();
  const unsigned long wifiTimeout = 10000; // Max 10 seconds to link to router
  bool wifiConnected = false;

  while (millis() - wifiStart < wifiTimeout) {
    network.handle(); // Keep OTA and background network stack ticking
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      break;
    }
    delay(50); // Yield to prevent CPU thrashing
  }

  // 4. PHASE 2: EXPLICIT STATE PUMPING HANDSHAKE WITH BLYNK SERVER
  bool serverConnected = false;

  if (wifiConnected) {
    // Crucial: Give the ESP32 network stack a brief moment to stabilize routing/IP tables
    delay(200); 
    dualPrintln("📡 Wi-Fi link established. Initiating Blynk cloud handshake...");
    
    // Signal Blynk's engine that we want to connect
    Blynk.connect(); 
    
    unsigned long blynkStart = millis();
    const unsigned long blynkTimeout = 6000; // 6-second window to authenticate
    
    // Actively pump the state machine until connected or timeout reached
    while (millis() - blynkStart < blynkTimeout) {
      Blynk.run();      // Forces Blynk to process its background socket connection here
      network.handle(); // Keeps OTA and background tasks responsive
      
      if (Blynk.connected()) {
        serverConnected = true;
        break;
      }
      delay(50); // Yield to prevent CPU thrashing
    }
  }

  // 5. IF SERVER IS UNREACHABLE, GO BACK TO SLEEP TO SAVE BATTERY
  if (!serverConnected) {
    dualPrintln("❌ Network or Blynk server unreachable. Re-entering power save mode...");
    return; // Exit to main loop; safety gate will put it right back to sleep
  }

  dualPrintln("📡 Server linked. Pulling Standby Pin (V17) state...");

  // 6. THE 3-SECOND PACKET SYNC WINDOW
  // Give the server a clear window to return the value and trigger BLYNK_WRITE(V17)
  // BLYNK_CONNECTED() automatically requests the sync on link. 
  // We just spin here briefly to receive and process that specific packet.
  unsigned long startSyncWait = millis();
  while (millis() - startSyncWait < 3000) {
    Blynk.run(); // This actively processes the incoming BLYNK_WRITE packet!
    
    // If the button was OFF, BLYNK_WRITE(V17) triggers the 'else' block, 
    // which sets isStandbyActive to false. If that happens, we exit immediately!
    if (!isStandbyActive) {
      dualPrintln("🔓 Wake confirmation parsed successfully!");
      return; 
    }
    delay(10);
  }

  // 7. DEFAULT FALLBACK
  // If 3 seconds pass and isStandbyActive is STILL true, it means either:
  // A) The switch is still HIGH (User wants it asleep)
  // B) The sync packet dropped. Safety first: go back to sleep.
  dualPrintln("🔒 Standby confirmation or sync timeout reached. Returning to sleep...");
}