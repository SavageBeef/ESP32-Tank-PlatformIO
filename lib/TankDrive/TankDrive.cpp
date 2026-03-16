#include "TankDrive.h"

TankCommand TankDrive::calculate(int x, int y, bool isHaltedByUSonic, XRegion lastXRegion, int maxSpeed, int minSpeed, int noSpeed) {
    TankCommand cmd;
    
    // 1. Brain Persistence
    // Default nextXRegion to the last state to keep the guard "sticky"
    cmd.nextXRegion = lastXRegion;
    cmd.state = STATE_STOP;

    // Constants for your specific dead zones
    const int maxRange = 712;
    const int minRange = 312;
    const int maxNeuRange = 612;
    const int minNeuRange = 412;

    // 2. Identify Current Intent
    XRegion currentXIntent = X_NEUTRAL;
    if (x >= maxNeuRange) currentXIntent = X_FORWARD;
    else if (x <= minNeuRange) currentXIntent = X_BACKWARD;

    // 3. THE HARDWARE LOCK (STATE_GUARD)
    
    // Check Case A: Illegal Flip (FWD to BWD or vice versa)
    if ((lastXRegion == X_FORWARD && currentXIntent == X_BACKWARD) ||
        (lastXRegion == X_BACKWARD && currentXIntent == X_FORWARD)) {
        cmd.state = STATE_GUARD; 
        return cmd; // Main file hits 'return', keeping last PWM and Directional Pins
    }

    // Check Case B: Edge Center Strips (Around the Edge)
    // If X is centered but Y is at an extreme, we stay in GUARD
    if (currentXIntent == X_NEUTRAL && (y > maxNeuRange || y < minNeuRange)) {
        cmd.state = STATE_GUARD;
        return cmd; 
    }

    // Check Case C: True Neutral (The ONLY place the guard resets)
    if (currentXIntent == X_NEUTRAL && (y >= minNeuRange && y <= maxNeuRange)) {
        cmd.state = STATE_STOP;
        cmd.nextXRegion = X_NEUTRAL; 
        cmd.leftPWM = noSpeed; cmd.rightPWM = noSpeed;
        cmd.motorL_Pos = 0; cmd.motorL_Neg = 0;
        cmd.motorR_Pos = 0; cmd.motorR_Neg = 0;
        return cmd;
    }

    // 4. Ultrasonic Halt - Obstacle detection (Only block FORWARD movement)
    if (isHaltedByUSonic && currentXIntent == X_FORWARD) {
        cmd.state = STATE_HALTED;
        cmd.nextXRegion = X_NEUTRAL; 
        cmd.leftPWM = noSpeed; cmd.rightPWM = noSpeed;
        cmd.motorL_Pos = 0; cmd.motorL_Neg = 0;
        cmd.motorR_Pos = 0; cmd.motorR_Neg = 0;
        return cmd; 
    }

// 5. Movement Logic
    // FWD
    if (currentXIntent == X_FORWARD) {
        // Set State and Polarity Once
        cmd.state = STATE_FWD;
        cmd.nextXRegion = X_FORWARD;
        cmd.motorL_Pos = 1; cmd.motorL_Neg = 0;
        cmd.motorR_Pos = 0; cmd.motorR_Neg = 1;

        // Forward Center
        if (x >= maxRange && y >= minRange && y <= maxRange) {
            cmd.leftPWM = maxSpeed; cmd.rightPWM = maxSpeed;
        } 
        // Forward Left
        else if (x >= maxRange && y >= maxNeuRange) {
            cmd.leftPWM = minSpeed; cmd.rightPWM = maxSpeed;
        } 
        // Hard Forward Left
        else if (x > maxNeuRange && x < maxRange && y > maxRange) {
            cmd.leftPWM = noSpeed; cmd.rightPWM = maxSpeed;
        } 
        // Forward Right
        else if (x >= maxRange && y <= minNeuRange) {
            cmd.leftPWM = maxSpeed; cmd.rightPWM = minSpeed;
        } 
        // Hard Forward Right
        else if (x > maxNeuRange && x < maxRange && y < minRange) {
            cmd.leftPWM = maxSpeed; cmd.rightPWM = noSpeed;
        }
        // This block shouldn't execute unless something went totally wrong
        else {
            // Default Forward PWM if none of the above are met
            cmd.leftPWM = maxSpeed; cmd.rightPWM = maxSpeed;
        }
    }
    // BWD 
    else if (currentXIntent == X_BACKWARD) {
        // Set State and Polarity Once
        cmd.state = STATE_BWD;
        cmd.nextXRegion = X_BACKWARD;
        cmd.motorL_Pos = 0; cmd.motorL_Neg = 1;
        cmd.motorR_Pos = 1; cmd.motorR_Neg = 0;

        // Backward Center
        if (y >= minRange && y <= maxRange && x <= minRange) {
            cmd.leftPWM = maxSpeed; cmd.rightPWM = maxSpeed;
        }
        // Backward Right
        else if (y <= minNeuRange && x <= minRange) {
            cmd.leftPWM = maxSpeed; cmd.rightPWM = minSpeed;
        }
        // Hard Backward Right
        else if (x > minRange && x < minNeuRange && y < minRange) {
            cmd.leftPWM = maxSpeed; cmd.rightPWM = noSpeed;
        } 
        // Backward Left
        else if (x <= minRange && y >= maxNeuRange) {
            cmd.leftPWM = minSpeed; cmd.rightPWM = maxSpeed;
        }
        // Hard Backward left
        else if (x > minRange && x < minNeuRange && y > maxRange) {
            cmd.leftPWM = noSpeed; cmd.rightPWM = maxSpeed;
        }
        // This block shouldn't execute unless something went totally wrong
        else {
            // Default Backward PWM if none of the above are met
            cmd.leftPWM = maxSpeed; cmd.rightPWM = maxSpeed;
        }
    }
    return cmd;
}


