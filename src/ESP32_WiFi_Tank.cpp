/*************************************************************
  Download latest Blynk library here:
    https://github.com/blynkkk/blynk-library/releases/latest

  Blynk is a platform with iOS and Android apps to control
  Arduino, Raspberry Pi and the likes over the Internet.
  You can easily build graphic interfaces for all your
  projects by simply dragging and dropping widgets.

    Downloads, docs, tutorials: http://www.blynk.cc
    Sketch generator:           http://examples.blynk.cc
    Blynk community:            http://community.blynk.cc
    Follow us:                  http://www.fb.com/blynkapp
                                http://twitter.com/blynk_app

  Blynk library is licensed under MIT license
  This example code is in public domain.

 *************************************************************
  This example runs directly on ESP32 chip.

  Note: This requires ESP32 support package:
    https://github.com/espressif/arduino-esp32

  Please be sure to select the right ESP32 module
  in the Tools -> Board menu!

  Change WiFi ssid (line 62), pass (line 63), and Blynk auth token (line 58) to run :)
  If you're using custom server, also change IPAddress (line 135). 
  Feel free to apply it to any other example. It's simple!
 *************************************************************/

#include <Arduino.h>
#include "TankDrive.h" //Tank Core Logic
#include "BatteryMath.h"
#include "NetworkHelper.h" // WIFI, OTA, WebSerial, Captive Portals

/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial

#include <BlynkSimpleEsp32.h>

// Add printf functionality to WebSerial
#include "WebSerial_printf.h"

// Dual Printf to Serial Monitor and WebSerial Monitor
#include "dual_printf.h"

int LED = 2;

// Motor 1 (Left)
#define motorL_Negative 22
#define motorL_Positive 23 
#define motorL_EN 21    

// Motor 2 (Right)
#define motorR_Negative 27
#define motorR_Positive 26 
#define motorR_EN 25

// Analog speeds from 0 (lowest) - 1023 (highest).
// 3 speeds used -- 0 (noSpeed), 750 (minSpeed), 1023 (maxSpeed).
// Use whatever speeds you want...too fast made it a pain in the ass to control.
int minSpeed = 750;
int maxSpeed = 1023;
int noSpeed = 0;

// Neutral zone settings for x and y.
// Joystick must move outside these boundary numbers (in the blynk client app) to activate the motors.
// Makes it a easier to control the tank.
int minRange = 312;
int maxRange = 712;
int minNeuRange = 412;
int maxNeuRange = 612;

// Global state tracking for the "Directional Guard"
XRegion lastXRegion = X_NEUTRAL;

// Setting PWM properties for motors.
int freq = 5000; // Was 30000 
int resolution = 10; // Was 10

// Setting up Ultrasonic Sensor.
#include <SR04.h>
#define TRIG_PIN 18
#define ECHO_PIN 19
SR04 sr04 = SR04(ECHO_PIN,TRIG_PIN);
int distance;

// Timer to help run Ultrasonic button state check.
BlynkTimer timer;
void uSonicButtonCheck();
int uSonicState;
bool isHaltedByUSonic = false;
int ultrasonicLimit = 40; // Default value in cm

// Lights.
#define lights 16 // Signal to NPN2 Transistor.
WidgetLED ledIndicator(V11);

// Control ultrasonic NPN1 Transistor.
#define currentToUSonic 5

// Pin for battery voltage.
#define batteryVoltagePin 35
void readBatteryVoltage();

// Battery Parameters.
const float maxBatteryVoltage = 16.68;  // Max voltage for a 18650 3.7v Li-ion cell. 4.2V/cell. 4.17V/cell measured.
const float minBatteryVoltage = 12;  // Min voltage for a 18650 3.7 Li-ion cell. 3V/cell.
// Voltage Divider Parameters.
const float R1 = 41860.0;  // Resistor R1 in voltage divider (42kΩ), multimeter measured value provided.
const float R2 = 9989.0;  // Resistor R2 in voltage divider (10kΩ), multimeter measured value provided.
// Factor to correct for ADC inaccuracies.
const float calibrationFactor = 1.025; // = Voltage reading from multimeter / Voltage after voltage divider.

// Function not needed.
// Arduino like analogWrite.
// Value has to be between 0 and valueMax.
void ledcAnalogWrite(uint8_t channel, uint32_t value, uint32_t valueMax = 255) {
  // calculate duty, 4095 from 2 ^ 12 - 1
  uint32_t duty = (1023 / valueMax) * min(value, valueMax);

  // write duty to LEDC
  ledcWrite(channel, duty);
}

// Print to both Serial Monitor and WebSerial Monitor.
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

// Class Instantiation
NetworkHelper network;

// Declare functions
void applyToMotors(TankCommand cmd);

void setup()
{
  // Debug console
  Serial.begin(115200);
  
  // Give serial time to initialize and wait for monitor to connect
  delay(2000);
  Serial.println("\n\n=== TANK BOOTING ===");
  Serial.println("Initializing systems...");

  network.begin("SavageTank", "password123"); // Manages WIFI, Captive Portals, Web Serial setup and Arduino OTA setup

  // --- 4. ATTEMPT BLYNK CONNECTION ---
  Serial.println("\n--- Blynk Connection Phase ---");

  // Convert port char array to integer for the function
  int port_int = atoi(blynk_port);

  Serial.print("Attempting Blynk Server: ");
  Serial.print(blynk_server);
  Serial.print(":");
  Serial.println(port_int);

  // IP and Port of Blynk Server.
  Blynk.config(blynk_auth, blynk_server, port_int);

  // Try to connect to Blynk server
  int blynk_startup_attempts = 0;
  while (!Blynk.connected() && blynk_startup_attempts < 5) {
    Serial.print(".");
    Blynk.connect(4000); // 4 second timeout per attempt
    blynk_startup_attempts++;
  }

  if (!Blynk.connected()) {
    Serial.println("\nBlynk server connection failed. Launching Blynk Provisioner...");
    network.launchBlynkProvisioner();

    // Try to reconnect with new credentials
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

  pinMode(2, OUTPUT);
  
  pinMode(motorL_Negative, OUTPUT);
  pinMode(motorL_Positive, OUTPUT);
  pinMode(motorL_EN, OUTPUT);
  pinMode(motorR_Negative, OUTPUT);
  pinMode(motorR_Positive, OUTPUT);
  pinMode(motorR_EN, OUTPUT);

  pinMode(lights, OUTPUT);

  pinMode(currentToUSonic, OUTPUT);

  pinMode(batteryVoltagePin, INPUT);
  
  
  // Attach PWM functionalitites via ledc to the GPIO to be controlled.
  ledcAttach(motorL_EN, freq, resolution);
  ledcAttach(motorR_EN, freq, resolution);
  ledcAttach(lights, 1000, resolution);

  // Blink Lights.
  ledcWrite(lights, 0);
  delay(250);
  ledcWrite(lights, 511);
  delay(250);
  ledcWrite(lights, 0);
  delay(250);
  ledcWrite(lights, 766);
  delay(250);
  ledcWrite(lights, 0);

  // Set a function to be called every 50ms.
  timer.setInterval(50L, uSonicButtonCheck); 

  // Set a function to be called every 1s.
  timer.setInterval(1000L, readBatteryVoltage);

  // Set default value for ultrasonic limit in app.
  Blynk.virtualWrite(V8, ultrasonicLimit); 
}

void loop()
{
  // Failsafe to halt the tank when connection is loss.
  if (WiFi.status() != WL_CONNECTED || !Blynk.connected()) {
    ledcWrite(motorL_EN, noSpeed);
    ledcWrite(motorR_EN, noSpeed);
  }
  network.handle(); // Manages OTA
  // Check if WiFi is connected before attempting to connect to Blynk
  if (WiFi.status() == WL_CONNECTED) {
    // If not connected to Blynk, try to connect
    if (!Blynk.connected()) {
      blynk_connection_attempts++;
      // If we've failed too many times and not already provisioning, launch provisioner
      if (blynk_connection_attempts >= BLYNK_MAX_ATTEMPTS && !has_provisioned_blynk) {
        Serial.println("Blynk connection failed multiple times. Launching Blynk Provisioner...");
        has_provisioned_blynk = true; // Prevent repeated provisioning attempts
        network.launchBlynkProvisioner();
        // Reconfigure and try again
        int port_int = atoi(blynk_port);
        Blynk.config(blynk_auth, blynk_server, port_int);
        blynk_connection_attempts = 0; // Reset counter
      }
      // Blink onboard led on attempts to connect to blynk server.
      digitalWrite(2, HIGH); 
      delay(500);
      digitalWrite(2, LOW);
      delay(500);
      Serial.println("Attempting to connect to Blynk server...");
      Blynk.connect(5000); // 5 sec timeout
    }
    // If connected, run the Blynk routine
    else {
      blynk_connection_attempts = 0; // Reset on successful connection
      Blynk.run(); // Run Blynk
      timer.run(); // Run BlynkTimer
    }
  }
}

BLYNK_CONNECTED() {
    Blynk.syncAll(); // Sync blynk client app with blynk server to recall last values.
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