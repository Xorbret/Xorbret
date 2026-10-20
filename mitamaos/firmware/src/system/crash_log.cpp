// Panic hook for CrashLog (see crash_log.h). Linked with
// -Wl,--wrap=esp_panic_handler, so every panic passes through here first.
#include "crash_log.h"
#include <SD.h>
#include <M5Cardputer.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_debug_helpers.h>
#include <esp_private/panic_internal.h>
#include <freertos/xtensa_context.h>
#include "../config.h"

#ifndef MITAMA_BUILD
#define MITAMA_BUILD "dev"
#endif

namespace CrashLog {

static RTC_NOINIT_ATTR Record s_rec;
static RTC_NOINIT_ATTR char s_crumb[28];
static Record s_last;                // what boot() found, for show()
static esp_reset_reason_t s_reason;

Record& rtc() { return s_rec; }

static char s_base[28] = "";

void crumb(const char* what) {
    size_t i = 0;
    for (; what[i] && i < sizeof(s_crumb) - 1; i++) s_crumb[i] = what[i];
    s_crumb[i] = 0;
}
void base(const char* what) { snprintf(s_base, sizeof(s_base), "%s", what); crumb(s_base); }
void done() { crumb(s_base); }

static const char* _causeName(uint32_t c) {
    switch (c) {
        case 0:  return "illegal instruction";
        case 2:  return "instruction fetch error";
        case 3:  return "load/store error";
        case 6:  return "divide by zero";
        case 9:  return "unaligned access";
        case 28: return "bad read (null pointer?)";
        case 29: return "bad write (null pointer?)";
        case 0xFF: return "abort / watchdog / assert";
        default: return "exception";
    }
}

static const char* _resetName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_PANIC:    return "crash";
        case ESP_RST_INT_WDT:  return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_WDT:      return "watchdog";
        case ESP_RST_BROWNOUT: return "brownout (power dipped)";
        default:               return "reset";
    }
}

bool boot() {
    s_reason = esp_reset_reason();
    bool crashed = s_reason == ESP_RST_PANIC || s_reason == ESP_RST_INT_WDT || s_reason == ESP_RST_TASK_WDT
                || s_reason == ESP_RST_WDT || s_reason == ESP_RST_BROWNOUT;
    if (s_rec.magic == MAGIC) s_last = s_rec;
    else { memset(&s_last, 0, sizeof(s_last)); memcpy(s_last.crumb, s_crumb, sizeof(s_crumb)); }
    s_last.crumb[sizeof(s_last.crumb) - 1] = 0;
    s_rec.magic = 0;
    if (!crashed) { s_crumb[0] = 0; return false; }
    if (s_last.magic != MAGIC) {     // no panic record (brownout, hardware watchdog): keep the crumb
        memcpy(s_last.crumb, s_crumb, sizeof(s_crumb));
        s_last.crumb[sizeof(s_last.crumb) - 1] = 0;
    }
    if (SD.cardType() != CARD_NONE) {
        if (!SD.exists("/MitamaOS/logs")) SD.mkdir("/MitamaOS/logs");
        File f = SD.open("/MitamaOS/logs/crash.txt", FILE_APPEND);
        if (f) {
            f.printf("--- build %s: %s\n", MITAMA_BUILD, _resetName(s_reason));
            if (s_last.magic == MAGIC) {
                f.printf("cause %lu (%s) core %u pc 0x%08lx addr 0x%08lx\nbacktrace:",
                         (unsigned long)s_last.cause, _causeName(s_last.cause), s_last.core,
                         (unsigned long)s_last.pc, (unsigned long)s_last.excvaddr);
                for (int i = 0; i < s_last.btN; i++) f.printf(" 0x%08lx", (unsigned long)s_last.bt[i]);
                f.print("\n");
            }
            f.printf("doing: %s\n", s_last.crumb[0] ? s_last.crumb : "?");
            f.close();
        }
    }
    s_crumb[0] = 0;
    return true;
}

void show() {
    auto& d = M5Cardputer.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextSize(1);
    d.setFont(&fonts::Font0);
    d.setTextColor(TFT_RED, TFT_BLACK);
    d.setCursor(4, 4);
    d.printf("MitamaOS restarted: %s\n", _resetName(s_reason));
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setCursor(4, 18);
    d.printf("build %s\n", MITAMA_BUILD);
    d.setCursor(4, 30);
    d.printf("doing: %s\n", s_last.crumb[0] ? s_last.crumb : "?");
    int y = 42;
    if (s_last.magic == MAGIC) {
        d.setCursor(4, y); d.printf("%s", _causeName(s_last.cause)); y += 12;
        d.setCursor(4, y); d.printf("pc %08lx  addr %08lx", (unsigned long)s_last.pc, (unsigned long)s_last.excvaddr); y += 12;
        d.setTextColor(TFT_CYAN, TFT_BLACK);
        for (int i = 0; i < s_last.btN; i++) {
            d.setCursor(4 + (i % 3) * 78, y + (i / 3) * 11);
            d.printf("%08lx", (unsigned long)s_last.bt[i]);
        }
    }
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.setCursor(4, 124);
    d.print("Photo this for Claude. Any key goes on.");
    uint32_t t0 = millis();
    delay(500);
    while (millis() - t0 < 60000) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isPressed()) break;
        delay(20);
    }
    while (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); }
}

} // namespace CrashLog

// ── the hook ──────────────────────────────────────────────────────────────
// Runs inside the panic, maybe with the flash cache off: IRAM code, RTC
// data, no library calls except IRAM ones.
extern "C" void __real_esp_panic_handler(panic_info_t* info);

// Return address -> the call instruction's address (Xtensa windowed ABI
// keeps the window size in the top two bits).
static inline uint32_t IRAM_ATTR _callPc(uint32_t pc) {
    if (pc & 0x80000000) pc = (pc & 0x3fffffff) | 0x40000000;
    return pc - 3;
}

extern "C" void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t* info) {
    using namespace CrashLog;
    Record& r = s_rec;
    r.magic = MAGIC;
    r.core = info->core;
    r.btN = 0;
    r.cause = 0xFF; r.pc = 0; r.excvaddr = 0;
    const XtExcFrame* fr = (const XtExcFrame*)info->frame;
    if (fr) {
        if (info->exception == PANIC_EXCEPTION_FAULT) r.cause = fr->exccause;
        r.pc = fr->pc;
        r.excvaddr = fr->excvaddr;
        esp_backtrace_frame_t bt;
        memset(&bt, 0, sizeof(bt));
        bt.pc = fr->pc; bt.sp = fr->a1; bt.next_pc = fr->a0;
        r.bt[r.btN++] = bt.pc;                     // the faulting instruction itself
        while (r.btN < BT_MAX && bt.next_pc && esp_backtrace_get_next_frame(&bt))
            r.bt[r.btN++] = _callPc(bt.pc);
    }
    for (size_t i = 0; i < sizeof(r.crumb); i++) r.crumb[i] = s_crumb[i];
    __real_esp_panic_handler(info);
}
