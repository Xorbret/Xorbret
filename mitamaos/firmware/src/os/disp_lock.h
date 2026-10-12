#pragma once
// disp_lock.h — shared mutex guarding all writes to the *physical* display
// (canvas.pushSprite(0,0) and friends). Needed because the Lua REPL runs its
// own FreeRTOS task (see lua_app.h, lua_core1_task) which can call pushSprite
// concurrently with the main UI loop. Without this, two tasks can race
// inside the SPI driver's own transaction lock and trip a FreeRTOS assert
// (xQueueGenericSend / mutex given by non-owner task) which hard-crashes
// and reboots the board.
//
// Usage: wrap every call that ends up doing canvas.pushSprite(0,0) with
//   { DispLock _lk; canvas.pushSprite(0,0); }
// or just:
//   dispLockTake(); canvas.pushSprite(0,0); dispLockGive();

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t g_dispMutex = nullptr;

static inline void dispLockInit() {
    if (!g_dispMutex) g_dispMutex = xSemaphoreCreateMutex();
}
static inline void dispLockTake() {
    if (g_dispMutex) xSemaphoreTake(g_dispMutex, portMAX_DELAY);
}
static inline void dispLockGive() {
    if (g_dispMutex) xSemaphoreGive(g_dispMutex);
}

// RAII helper: `DispLock _lk;` at the top of a scope holds the lock until
// the scope ends.
struct DispLock {
    DispLock()  { dispLockTake(); }
    ~DispLock() { dispLockGive(); }
};
