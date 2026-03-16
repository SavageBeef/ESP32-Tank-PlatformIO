#ifndef BATTERY_MATH_H
#define BATTERY_MATH_H

class BatteryMath {
public:
    static float calculateVoltage(float milliVolts, float R1, float R2, float cal) {
        // Standard Voltage Divider Formula: V_in = V_out * (R1 + R2) / R2
        float voltageAtPin = milliVolts / 1000.0;
        return (voltageAtPin * (R1 + R2) / R2) * cal;
    }

    static float calculatePercentage(float currentV, float minV, float maxV) {
        float pct = (currentV - minV) / (maxV - minV) * 100.0;
        // Clamp between 0 and 100 using Arduino's constrain (for floats)
        return (pct < 0) ? 0.0 : (pct > 100) ? 100.0 : pct;
    }
};

#endif