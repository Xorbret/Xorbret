#pragma once
#include "app.h"
#include "mem_guard.h"
#include "../config.h"

// App registry and dispatcher (singleton: AppManager::get()).
//
// Thread-safety note: launchByIndex()/goHome()/launch() touch _active and
// call onStop()/onStart() on apps, which is only safe to do from core0 (the
// main Arduino loop() thread). Some apps (e.g. Lua) run user code on core1
// and must NOT call these directly. Use requestHome() / requestLaunch()
// instead — they just record a request that core0's tick() picks up and
// acts on safely on its next iteration.
class AppManager {
public:
    static AppManager& get() {
        static AppManager instance;
        return instance;
    }

    void registerApp(App* app) {
        if (_count < MAX_APPS) _apps[_count++] = app;
        else Serial.printf("[apps] MAX_APPS full, dropped %s\n", app->getName());
    }

    // Only call from core0.
    bool launch(const char* name) {
        for (int i = 0; i < _count; i++) {
            if (strcmp(_apps[i]->getName(), name) == 0) {
                return launchByIndex(i);
            }
        }
        return false;
    }

    // Only call from core0.
    bool launchByIndex(int idx) {
        if (idx < 0 || idx >= _count) return false;
        if (_active) {
            uint32_t heapBeforeStop = ESP.getFreeHeap();
            const char* leavingName = _active->getName();
            _active->onStop();
            MemGuard::checkAfterStop(leavingName, heapBeforeStop);
        }
        _active = _apps[idx];
        _activeIdx = idx;
        _active->onStart();
        MemGuard::poll();
        return true;
    }

    // Only call from core0.
    void goHome() { launchByIndex(0); }

    // Safe to call from ANY core/task: just records a request. core0's
    // tick() performs the actual switch on its next iteration.
    void requestHome() { _pendingLaunch = 0; _launchRequested = true; }
    void requestLaunchByIndex(int idx) { _pendingLaunch = idx; _launchRequested = true; }
    bool requestLaunch(const char* name) {
        for (int i = 0; i < _count; i++) {
            if (strcmp(_apps[i]->getName(), name) == 0) {
                requestLaunchByIndex(i);
                return true;
            }
        }
        return false;
    }

    void tick() {
        MemGuard::poll();
        if (_launchRequested) {
            _launchRequested = false;
            launchByIndex(_pendingLaunch);
            return;
        }
        if (_active) _active->update();
    }

    App*       getActive()    const { return _active; }
    int        getActiveIdx() const { return _activeIdx; }
    App*       getApp(int i)  const { return (i < _count) ? _apps[i] : nullptr; }
    int        getCount()     const { return _count; }

private:
    AppManager() = default;
    App*  _apps[MAX_APPS] = {};
    int   _count     = 0;
    App*  _active    = nullptr;
    int   _activeIdx = -1;
    volatile bool _launchRequested = false;
    volatile int  _pendingLaunch   = 0;
};
