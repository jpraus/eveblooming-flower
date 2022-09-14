#pragma once

#include "Arduino.h"
#include "Config.h"
#include "hardware/Floower.h"
#include "connect/RemoteControl.h"
#include "behavior/Behavior.h"

#define STATE_STANDBY 0
#define STATE_OFF 1
#define STATE_LOW_BATTERY 2
#define STATE_BLUETOOTH_PAIRING 3
#define STATE_REMOTE_CONTROL 4
#define STATE_UPDATE_INIT 5
#define STATE_UPDATE_RUNNING 6
// states 128+ are reserved for child behaviors

class SmartPowerBehavior : public Behavior {
    public:
        SmartPowerBehavior(Config *config, Floower *floower, RemoteControl *remoteControl);
        virtual void setup(bool wokeUp = false);
        virtual void loop();
        virtual bool isIdle();
        virtual void runUpdate(String firmwareUrl);
        
    protected:
        virtual bool onLeafTouch(FloowerTouchEvent event);
        virtual void onRemoteControl();
        virtual bool canInitializeBluetooth();

        void changeStateIfIdle(state_t fromState, state_t toState);
        void changeState(uint8_t newState);
        HsbColor nextRandomColor();

        Config *config;
        Floower *floower;
        RemoteControl *remoteControl;

        uint8_t state;
        bool preventTouchUp = false;

    private:
        void enablePeripherals(bool initial, bool wokeUp);
        void disablePeripherals();        
        void powerWatchDog(bool initial = false, bool wokeUp = false);
        void planDeepSleep(long timeoutMs);
        void enterDeepSleep();
        void indicateStatus(bool charging);

        PowerState powerState;
        unsigned long colorsUsed = 0; // used by nextRandomColor

        unsigned long watchDogsTime = 0;
        unsigned long updateStatusTime = 0;
        unsigned long bluetoothStartTime = 0;
        unsigned long wifiStartTime = 0;
        unsigned long deepSleepTime = 0;

        uint8_t indicatingStatus = 0;

        String updateFirmwareUrl;
    
};