#include <unity.h>
#include <string>
#include "TankDrive.h"
#include "BatteryMath.h"


// Define constants to match your hardware setup
const int MAX_SPD = 1023;
const int MIN_SPD = 750;
const int NO_SPD = 0;

// Define the Struct
struct DriveTestCase {
    const char* label;
    int x, y;
    XRegion lastX;         // Input: Where was the stick?
    TankState expectedState;
    XRegion expectedNextX; // Output: Where does the brain think the stick is now?
    int expectedLeftPWM;
    int expectedRightPWM;
    int expL_Pos;
    int expL_Neg;
    int expR_Pos;
    int expR_Neg;
};

// Define the Array GLOBALLY
DriveTestCase cases[] = {
    // Label                 | X    | Y    | LastX      | ExpState      | ExpNextX    | L_PWM   | R_PWM  | L+ | L- | R+ | R- |
    {"Forward Center",       1023,  512,   X_NEUTRAL,   STATE_FWD,      X_FORWARD,    MAX_SPD,  MAX_SPD,  1,   0,   0,   1  },
    {"Forward Left",         886,   886,   X_NEUTRAL,   STATE_FWD,      X_FORWARD,    MIN_SPD,  MAX_SPD,  1,   0,   0,   1  },
    {"Hard Forward Left",    662,   1023,  X_NEUTRAL,   STATE_FWD,      X_FORWARD,    NO_SPD,   MAX_SPD,  1,   0,   0,   1  },
    {"Forward Right",        886,   129,   X_NEUTRAL,   STATE_FWD,      X_FORWARD,    MAX_SPD,  MIN_SPD,  1,   0,   0,   1  },
    {"Hard Forward Right",   662,   0,     X_NEUTRAL,   STATE_FWD,      X_FORWARD,    MAX_SPD,  NO_SPD,   1,   0,   0,   1  },
    
    {"Backward Center",      0,     512,   X_NEUTRAL,   STATE_BWD,      X_BACKWARD,   MAX_SPD,  MAX_SPD,  0,   1,   1,   0  },
    {"Backward Left",        129,   886,   X_NEUTRAL,   STATE_BWD,      X_BACKWARD,   MIN_SPD,  MAX_SPD,  0,   1,   1,   0  },
    {"Hard Backward Left",   362,   1023,  X_NEUTRAL,   STATE_BWD,      X_BACKWARD,   NO_SPD,   MAX_SPD,  0,   1,   1,   0  },
    {"Backward Right",       129,   147,   X_NEUTRAL,   STATE_BWD,      X_BACKWARD,   MAX_SPD,  MIN_SPD,  0,   1,   1,   0  },
    {"Hard Backward Right",  362,   0,     X_NEUTRAL,   STATE_BWD,      X_BACKWARD,   MAX_SPD,  NO_SPD,   0,   1,   1,   0  },

    {"Neutral Zone",         512,   512,   X_NEUTRAL,   STATE_STOP,     X_NEUTRAL,    NO_SPD,   NO_SPD,   0,   0,   0,   0  },
    // Testing the Guard: Trying to jump from FWD to BWD
    {"Slam Guard BWD",       0,     512,   X_FORWARD,   STATE_GUARD,    X_FORWARD,    NO_SPD,   NO_SPD,   0,   0,   0,   0  },
    // Testing the Guard: Trying to jump from BWD to FWD
    {"Slam Guard FWD",       1023,  512,   X_BACKWARD,  STATE_GUARD,    X_BACKWARD,   NO_SPD,   NO_SPD,   0,   0,   0,   0  },
    // Testing the Halt: Sensor blocked
    {"USonic Block FWD",     1023,  512,   X_FORWARD,   STATE_HALTED,   X_NEUTRAL,    NO_SPD,   NO_SPD,   0,   0,   0,   0  },
    {"USonic Allow BWD",     0,     512,   X_NEUTRAL,   STATE_BWD,      X_BACKWARD,   MAX_SPD,  MAX_SPD,  0,   1,   1,   0  }
};

// Pointer to track which case we are currently running
DriveTestCase currentCase;

void setUp(void) {}    // Satisfies the linker
void tearDown(void) {} // Satisfies the linker

void test_ultrasonic_halt(void) {
    // Scenario: Joystick is full forward, but ultrasonic sensor detects an obstacle
    TankCommand cmd = TankDrive::calculate(1023, 512, true, X_NEUTRAL, MAX_SPD, MIN_SPD, NO_SPD);
    
    TEST_ASSERT_EQUAL(STATE_HALTED, cmd.state);
    TEST_ASSERT_EQUAL(NO_SPD, cmd.leftPWM);
    TEST_ASSERT_EQUAL(NO_SPD, cmd.rightPWM);
}

void test_illegal_transition_guard(void) {
    // Scenario: Tank was moving FORWARD, user slams joystick to full BACKWARD (x=0)
    // The logic should block this and return STATE_STOP
    TankCommand cmd = TankDrive::calculate(0, 512, false, X_FORWARD, MAX_SPD, MIN_SPD, NO_SPD);
    
    TEST_ASSERT_EQUAL(STATE_GUARD, cmd.state);
    TEST_ASSERT_EQUAL(X_FORWARD, cmd.nextXRegion); // Should stay locked in FWD until center is hit
}

void test_neutral_zone_reset(void) {
    // Scenario: Joystick is in the "Absolute Dead Center" (512, 512)
    // The guard should reset to X_NEUTRAL
    TankCommand cmd = TankDrive::calculate(512, 512, false, X_FORWARD, MAX_SPD, MIN_SPD, NO_SPD);
    
    TEST_ASSERT_EQUAL(X_NEUTRAL, cmd.nextXRegion);
}

// The Wrapper Function
void run_single_case(void) {
    // Handle Ultrasonic specially for the last 2 cases
    std::string label = currentCase.label;
    bool halt = (label.find("USonic") != std::string::npos || label.find("Halt") != std::string::npos);
    
    // 1. Run the core logic
    TankCommand cmd = TankDrive::calculate(
        currentCase.x, currentCase.y, halt, 
        currentCase.lastX, MAX_SPD, MIN_SPD, NO_SPD
    );

    // This prints to the -v console during the test run
    UnityPrint("Testing Case: ");
    UnityPrint(currentCase.label);
    UNITY_OUTPUT_CHAR('\n');

    // 2. ASSERT THE BRAIN: Always verify State and NextXRegion
    TEST_ASSERT_EQUAL_INT_MESSAGE(currentCase.expectedState, cmd.state, currentCase.label);
    TEST_ASSERT_EQUAL_INT_MESSAGE(currentCase.expectedNextX, cmd.nextXRegion, currentCase.label);

    // 3. ASSERT THE HARDWARE: Only check pins if NOT in a Guard state
    // We only validate PWM/Pins for actual movement or intentional stops
    if (cmd.state != STATE_GUARD) {
        // PWM pins
        TEST_ASSERT_EQUAL_INT_MESSAGE(currentCase.expectedLeftPWM, cmd.leftPWM, currentCase.label);
        TEST_ASSERT_EQUAL_INT_MESSAGE(currentCase.expectedRightPWM, cmd.rightPWM, currentCase.label);
        // Polarity pins
        TEST_ASSERT_EQUAL_INT_MESSAGE(currentCase.expL_Pos, cmd.motorL_Pos, currentCase.label);
        TEST_ASSERT_EQUAL_INT_MESSAGE(currentCase.expL_Neg, cmd.motorL_Neg, currentCase.label);
        TEST_ASSERT_EQUAL_INT_MESSAGE(currentCase.expR_Pos, cmd.motorR_Pos, currentCase.label);
        TEST_ASSERT_EQUAL_INT_MESSAGE(currentCase.expR_Neg, cmd.motorR_Neg, currentCase.label);
    }
}

void test_battery_math() {
    float R1 = 41860.0;
    float R2 = 9989.0;
    float cal = 1.0;
    
    // Test 1: Full Battery Simulation
    // If ADC reads 3230mV (approx), what is the voltage?
    float V = BatteryMath::calculateVoltage(3230, R1, R2, cal);
    // Based on your values, this should be around 16.68V
    TEST_ASSERT_FLOAT_WITHIN(0.1, 16.68, V);

    // Test 2: Percentage Clamping
    // Ensure 11.0V (below min) returns 0%, not a negative number
    float pct = BatteryMath::calculatePercentage(11.0, 12.0, 16.68);
    TEST_ASSERT_EQUAL_FLOAT(0.0, pct);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // Standard tests
    RUN_TEST(test_ultrasonic_halt);
    RUN_TEST(test_illegal_transition_guard);
    RUN_TEST(test_neutral_zone_reset);

    // Dynamic Matrix tests using labels as function names
    for (const auto& c : cases) {
        currentCase = c; 
        RUN_TEST(run_single_case);
    }

    // Battery test
    RUN_TEST(test_battery_math);

    return UNITY_END();
}