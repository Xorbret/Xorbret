#pragma once
#include <Arduino.h>

// Base class for PaperOS apps (launcher entry points).
class App {
public:
    virtual ~App() = default;

    virtual void onStart() = 0;
    virtual void onStop()  = 0;
    virtual void update()  = 0;

    // Optional lifecycle hooks, default no-op. Only AppManager can call
    // update() on the active app - there's no true multitasking here, so
    // these aren't about running in the background. They're for an app to
    // release/reacquire something scarce (a hardware peripheral, a large
    // buffer) around a brief interruption that isn't a full onStop()/
    // onStart() cycle - e.g. a system dialog or overlay borrowing the
    // screen. No app needs them yet; they exist so one can opt in without
    // changing the App interface again.
    virtual void onPause()  {}
    virtual void onResume() {}

    virtual const char* getName() const = 0;
    virtual const char* getIcon() const { return "*"; }
    virtual const char* getDesc() const { return ""; }
};
