#ifndef TANKDRIVE_H
#define TANKDRIVE_H

enum XRegion { X_FORWARD, X_BACKWARD, X_NEUTRAL };
enum TankState { STATE_FWD, STATE_BWD, STATE_STOP, STATE_HALTED, STATE_GUARD };

struct TankCommand {
    TankState state;
    int leftPWM;
    int rightPWM;
    int motorL_Pos;
    int motorL_Neg;
    int motorR_Pos;
    int motorR_Neg;
    XRegion nextXRegion;

};

class TankDrive {
public:
    static TankCommand calculate(int x, int y, bool isHaltedByUSonic, XRegion lastXRegion, int maxSpeed, int minSpeed, int noSpeed);
};

#endif