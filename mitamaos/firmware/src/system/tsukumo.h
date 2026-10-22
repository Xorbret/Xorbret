#pragma once
// Tsukumo: finds M5Stack Units on the Grove port (and on a Cap's Grove
// port) and names them, without disturbing them.
//
// Rules, from the module catalog (mitamaos/m5-module-catalog.md in the
// project files):
//  - Probing is an address ACK (a zero-length write) plus register reads.
//    Nothing is written that changes a Unit's outputs: no relay, DAC,
//    motor, PaHub mask (except restoring it to 0), bootloader (0xFD) or
//    address (0xFF) register, and never an I2C General Call.
//  - Before any I2C, the Grove lines are checked with the pins as inputs.
//    A line held low means a plain button/sensor Unit, so no I2C is tried.
//  - Unknown devices are listed by address and left alone.
//  - Plain GPIO, analog and LED Units can't be detected; MitamaOS never
//    drives the Grove pins by itself, so they're safe too.
//
// The scan runs on the main loop (same thread as the keyboard, which
// shares the internal bus). A full scan takes well under a second.
#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include "sd_settings.h"
#include <Wire.h>

namespace Tsukumo {

// True while Lua's i2c.* has Arduino's Wire running on the Grove port.
// Wire and M5's Ex_I2C share I2C controller 0, so the scanner stops Wire
// first and Lua starts it again on its next i2c call.
inline volatile bool luaWire = false;
// True while net/monster.h holds the Grove serial link to an M5MonsterC5.
inline volatile bool monsterLinked = false;

// Live-reading support for the detail page (see TsukumoApp).
enum Drv : uint8_t {
    D_NONE, D_SHT3X, D_SHT4X, D_BMP280, D_SCD4X, D_BH1750, D_MLX90614,
    D_RTC, D_RFID, D_TOF, D_JOY, D_CARDKB, D_ENCODER, D_M5FW,
    D_MONSTER,    // M5MonsterC5 on the Grove UART (addr UART_ADDR)
};

static constexpr uint8_t UART_ADDR = 0xFF;   // Found.addr for a serial Unit

enum Bus : uint8_t { B_GROVE, B_CAP, B_HUB };   // B_HUB: behind a PaHub on the Grove port

struct Found {
    uint8_t addr;
    uint8_t bus;
    uint8_t hubCh;        // PaHub channel when bus == B_HUB
    uint8_t hub;          // that PaHub's address
    uint8_t drv;
    const char* name;     // "Unit ENV III", or "Unknown device"
    const char* note;     // one line: what it is / why it's left alone
};

static constexpr int MAX_FOUND = 24;

struct Result {
    Found   dev[MAX_FOUND];
    int     n = 0;
    bool    scanned = false;
    bool    lineLow = false;      // a Grove line held low: plain-pin Unit
    bool    linesHigh = false;    // both lines pulled/driven high with nothing on I2C
    bool    capLora = false;      // Cap LoRa-1262/868 (I/O expander at 0x43)
    bool    gpsGrove = false;     // Grove port handed to the GPS
    bool    monster = false;      // M5MonsterC5 answered ping on the Grove UART
    // UART listen (on request only)
    int8_t  uartPin = -1;
    uint32_t uartBaud = 0;
    bool    uartNmea = false;
    uint16_t uartBytes = 0;
};

static constexpr int G_SDA = 2, G_SCL = 1;
static constexpr uint32_t HZ = 100000;

// ── low-level reads on a given bus ─────────────────────────────────────
struct Io {
    m5::I2C_Class& i2c;
    bool ack(uint8_t a) const { return i2c.scanID(a, HZ); }
    // Plain read with no register pointer written first.
    bool raw(uint8_t a, uint8_t* b, size_t n) const {
        if (!i2c.start(a, true, HZ)) { i2c.stop(); return false; }
        bool ok = i2c.read(b, n, true);
        i2c.stop();
        return ok;
    }
    bool reg(uint8_t a, uint8_t r, uint8_t* b, size_t n) const { return i2c.readRegister(a, r, b, n, HZ); }
    int  reg8(uint8_t a, uint8_t r) const { uint8_t v; return reg(a, r, &v, 1) ? v : -1; }
    bool reg16(uint8_t a, uint16_t r, uint8_t* b, size_t n) const {
        if (!i2c.start(a, false, HZ)) { i2c.stop(); return false; }
        bool ok = i2c.write((uint8_t)(r >> 8)) && i2c.write((uint8_t)r)
               && i2c.restart(a, true, HZ) && i2c.read(b, n, true);
        i2c.stop();
        return ok;
    }
    bool cmd(uint8_t a, const uint8_t* c, size_t n) const {
        if (!i2c.start(a, false, HZ)) { i2c.stop(); return false; }
        bool ok = i2c.write(c, n);
        i2c.stop();
        return ok;
    }
    bool cmd16(uint8_t a, uint16_t c) const { uint8_t b[2] = { (uint8_t)(c >> 8), (uint8_t)c }; return cmd(a, b, 2); }
    // M5's STM32 Units read back their own address from 0xFF.
    bool m5unit(uint8_t a) const { return reg8(a, 0xFF) == a; }
};

// Sensirion CRC-8 (SHT, SCD, SGP).
static inline uint8_t crc8(const uint8_t* d, int n) {
    uint8_t c = 0xFF;
    for (int i = 0; i < n; i++) { c ^= d[i]; for (int b = 0; b < 8; b++) c = c & 0x80 ? (c << 1) ^ 0x31 : c << 1; }
    return c;
}
static inline bool sensirion(const uint8_t* d, int words) {
    for (int i = 0; i < words; i++) if (crc8(d + i * 3, 2) != d[i * 3 + 2]) return false;
    return true;
}

// Tells an SHT3x from an SHT4x by asking each for a measurement.
static inline uint8_t shtKind(const Io& io, uint8_t a) {
    uint8_t b[6];
    if (io.cmd16(a, 0x2400)) { delay(16); if (io.raw(a, b, 6) && sensirion(b, 2)) return D_SHT3X; }
    uint8_t c = 0xFD;
    if (io.cmd(a, &c, 1)) { delay(10); if (io.raw(a, b, 6) && sensirion(b, 2)) return D_SHT4X; }
    return D_NONE;
}

// Name one address. `seen` is the ACK map of the same bus, for Units made
// of two chips (ENV III = 0x44 + 0x70). Only reads, except: SHT/SCD/SGP
// measurement commands, and restoring a PaHub's mask to 0.
static inline Found identify(const Io& io, uint8_t a, const bool* seen) {
    Found f{ a, B_GROVE, 0, 0, D_NONE, "Unknown device", "Not in MitamaOS's list. Left alone." };
    auto set = [&](const char* n, const char* note, uint8_t d = D_NONE) { f.name = n; f.note = note; f.drv = d; };
    uint8_t b[8] = {};
    switch (a) {
    case 0x11: if (io.m5unit(a)) set("Unit MQ (gas)", "Gas sensor. Its heater draws a lot of power.", D_M5FW); break;
    case 0x21:
        // QRCode's 16-bit registers would look like a motor command to an
        // HBridge, so this one is named without probing further.
        if (io.m5unit(a)) set("Unit QRCode / HBridge", "Barcode scanner or motor driver. Left alone.", D_M5FW);
        break;
    case 0x20: case 0x22: case 0x24: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        if (io.m5unit(a)) set("Unit HBridge", "Motor driver. Never driven by MitamaOS.", D_M5FW);
        break;
    case 0x23: if (io.raw(a, b, 2)) set("Unit DLight", "Ambient light sensor (BH1750).", D_BH1750); break;
    case 0x25: if (io.m5unit(a)) set("Unit 8Servos", "Servo driver. Never driven by MitamaOS.", D_M5FW); break;
    case 0x26:
        if (io.m5unit(a)) set("Unit Scales / Mini Scales", "Weight sensor.", D_M5FW);
        else set("Unit 4Relay", "Relay board. Never switched by MitamaOS.");
        break;
    case 0x27: set("Unit EXT.IO", "Pin expander (PCA9554). Pins left as inputs."); break;
    case 0x28: {
        int v = io.reg8(a, 0x37);
        if (v == 0x15) set("Unit RFID2", "13.56 MHz card reader (WS1850S). Reads card IDs.", D_RFID);
        else if (v == 0x88 || (v >= 0x90 && v <= 0x92)) set("Unit RFID", "13.56 MHz card reader (MFRC522). Reads card IDs.", D_RFID);
        break;
    }
    case 0x29: {
        int v = io.reg8(a, 0x92);
        if (v == 0x44 || v == 0x4D) { set("Unit Color", "Colour sensor (TCS34725)."); break; }
        if (io.reg8(a, 0xC0) == 0xEE) { set("Unit ToF", "Distance sensor (VL53L0X), up to 2 m.", D_TOF); break; }
        if (io.reg16(a, 0x010F, b, 1) && b[0] == 0xEA) { set("Unit ToF4M", "Distance sensor (VL53L1X), up to 4 m."); break; }
        break;
    }
    case 0x30: if (io.reg8(a, 0xFE) >= 0) set("Unit DigiClock", "4-digit display."); break;
    case 0x31: if (io.reg(a, 0x10, b, 6) && memcmp(b, "ad9833", 6) == 0) set("Unit DDS", "Signal generator. Output left alone."); break;
    case 0x32: if (io.reg8(a, 0x04) == 0x90 && io.reg8(a, 0x05) == 0x64) set("Unit Thermal2", "Thermal camera (MLX90640)."); break;
    case 0x33: set("Unit Thermal", "Thermal camera (MLX90640)."); break;
    case 0x3C: set("OLED / Glass2 display", "Small I2C screen. Not used by MitamaOS yet."); break;
    case 0x3D: set(io.reg8(a, 0xFE) >= 0 ? "Unit Glass" : "Unit Glass2", "Small I2C screen. Not used by MitamaOS yet."); break;
    case 0x3E: set("Unit LCD", "1.14\" screen. Not used by MitamaOS yet."); break;
    case 0x40: set("Unit Encoder / Scroll", "Rotary knob with a button.", D_ENCODER); break;
    case 0x41: {
        if (io.reg(a, 0xFF, b, 2) && b[0] == 0x22 && b[1] == 0x60) set("Unit INA226", "Current and voltage meter.");
        else if (b[0] == 0x41) set("Unit 8Encoder", "Eight rotary knobs.", D_M5FW);
        break;
    }
    case 0x42: if (io.m5unit(a)) set("Unit AC Measure", "Mains power meter. Read only.", D_M5FW); break;
    case 0x43: if (io.m5unit(a)) set("Unit 8Angle", "Eight knobs.", D_M5FW); break;
    case 0x44:
        if (seen[0x70]) set("Unit ENV III", "Temperature, humidity, pressure.", D_SHT3X);
        else { uint8_t k = shtKind(io, a);
               set(k == D_SHT4X ? "Unit ENV IV" : seen[0x76] ? "Unit ENV II" : "SHT30 sensor", "Temperature, humidity, pressure.", k ? k : D_SHT3X); }
        break;
    case 0x45: if (io.m5unit(a)) set("Unit EXT.IO2", "Pin expander. Pins left alone.", D_M5FW); break;
    case 0x46: if (io.m5unit(a)) set("Unit ByteSwitch", "Eight switches.", D_M5FW); break;
    case 0x47: if (io.m5unit(a)) set("Unit ByteButton", "Eight touch buttons.", D_M5FW); break;
    case 0x48:
        // ADS1100/1110 take any written byte as their config, so read first.
        if (seen[0x51]) { set("Unit AMeter", "Current meter (ADS1115)."); break; }
        if (io.raw(a, b, 3) && (b[2] & 0x60) == 0) { set("Unit ADC", "Voltage input (ADS1100)."); break; }
        if (io.m5unit(a)) set("Unit Step16", "16-position switch.", D_M5FW);
        break;
    case 0x49: if (seen[0x53]) set("Unit VMeter", "Voltage meter (ADS1115)."); break;
    case 0x50: {
        int v = io.reg8(a, 0x7F);   // ST25R3916: read (0x40) the IC identity register (0x3F)
        if (v >= 0 && (v >> 3) == 0x05 && (v & 7)) { set("Unit NFC", "NFC reader (ST25R3916). Detected only."); break; }
        if (io.reg8(a, 0x20) == 0x50) set("Unit ACSSR / DCSSR", "Mains relay. Never switched by MitamaOS.");
        break;
    }
    case 0x51: set(seen[0x48] ? "Unit AMeter (memory)" : "Unit RTC", seen[0x48] ? "Calibration memory of the AMeter." : "Real-time clock (BM8563).", seen[0x48] ? D_NONE : D_RTC); break;
    case 0x52: if (io.raw(a, b, 3)) set("Unit Joystick", "Joystick with a button.", D_JOY); break;
    case 0x53:
        if (io.reg8(a, 0x00) == 0xE5) set("Unit Accel", "Accelerometer (ADXL345).");
        else if (seen[0x49]) set("Unit VMeter (memory)", "Calibration memory of the VMeter.");
        break;
    case 0x55: if (io.m5unit(a)) set("Unit AIN 4-20mA", "Current-loop input.", D_M5FW); break;
    case 0x56:
        if (io.reg8(a, 0xD1) == 0x5C) set("Unit Mini BPS", "Air pressure (QMP6988).");
        else if (io.m5unit(a)) set("Unit TimerPWR", "Power timer. Never written by MitamaOS.", D_M5FW);
        break;
    case 0x57: {
        int v = io.reg8(a, 0xFF);
        set(v == 0x11 || v == 0x15 ? "Unit Heart" : "Unit Ultrasonic", v == 0x11 || v == 0x15 ? "Heart-rate sensor (MAX3010x)." : "Distance sensor.");
        break;
    }
    case 0x58: case 0x59: {
        if (a == 0x59 && io.m5unit(a)) { set("Unit ExtEncoder", "Encoder input.", D_M5FW); break; }
        uint8_t s[2] = { 0x36, 0x82 };
        if (a == 0x58 && io.cmd(a, s, 2)) { delay(2); if (io.raw(a, b, 9) && sensirion(b, 3)) { set("Unit TVOC/eCO2", "Air quality (SGP30)."); break; } }
        set("Unit DAC2", "0-10 V output. Never written by MitamaOS.");
        break;
    }
    case 0x5A:
        if (io.reg8(a, 0x0F) == 0xD3) { set("Unit TMOS PIR", "Presence sensor (STHS34PF80)."); break; }
        if (io.reg(a, 0x2E, b, 3) && b[0] == 0x5A) { set("Unit NCIR", "Contactless thermometer (MLX90614).", D_MLX90614); break; }
        if (io.m5unit(a)) set("Unit NCIR2", "Contactless thermometer.", D_M5FW);
        break;
    case 0x5F: set("Unit CardKB", "Mini keyboard.", D_CARDKB); break;
    case 0x60: set("Unit DAC", "Voltage output. Never written by MitamaOS."); break;
    case 0x61: set("Unit PbHub", "Six-port GPIO hub. Ports left alone."); break;
    case 0x62: {
        uint8_t st[2] = { 0x3F, 0x86 };   // stop periodic measurement, then ask the variant
        io.cmd(a, st, 2); delay(500);
        if (io.cmd16(a, 0x202F)) { delay(2); if (io.raw(a, b, 3) && sensirion(b, 1)) {
            set((b[0] >> 4) == 1 ? "Unit CO2L" : "Unit CO2", "CO2, temperature, humidity (SCD4x).", D_SCD4X); break; } }
        set("Unit CO2", "CO2, temperature, humidity (SCD4x).", D_SCD4X);
        break;
    }
    case 0x63: if (io.m5unit(a)) set("Unit Joystick2", "Joystick with a button.", D_M5FW); break;
    case 0x64: if (io.m5unit(a)) set("Unit Roller", "Motor. Never driven by MitamaOS.", D_M5FW); break;
    case 0x65: if (io.m5unit(a)) set("Unit BLDC Driver", "Motor. Never driven by MitamaOS.", D_M5FW); break;
    case 0x66:
        if (io.reg8(a, 0x04) == 0x31 && io.reg8(a, 0x05) == 0x85) set("Unit KMeter", "Thermocouple thermometer.");
        else if (io.m5unit(a)) set("Unit KMeter-ISO", "Thermocouple thermometer.", D_M5FW);
        break;
    case 0x68: case 0x69:
        if (io.reg8(a, 0x75) == 0x19) set("Unit Mini IMU", "Motion sensor (MPU6886).");
        else if (io.reg8(a, 0x00) == 0x24) set(seen[0x14] ? "Unit DoF10" : "Unit IMU Pro Mini", "Motion sensor (BMI270).");
        break;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
        if (a == 0x73) { if (io.reg8(a, 0x00) == 0x20) set("Unit Gesture", "Hand gesture sensor (PAJ7620)."); break; }
        if (a == 0x76 || a == 0x77) {
            int v = io.reg8(a, 0xD0);
            if (v == 0x58) set(seen[0x44] ? "ENV pressure sensor" : seen[0x68] ? "IMU Pro Mini barometer" : "Unit Mini BPS", "Air pressure (BMP280).", D_BMP280);
            else if (v == 0x61) set("Unit ENV Pro", "Temperature, humidity, pressure, gas (BME688).");
            else if (a == 0x76 && (io.reg8(a, 0x0D) & 0x0F) == 0x01) set("Unit DoF10 barometer", "Air pressure (SPA06).");
            break;
        }
        if (a == 0x70 && seen[0x44]) {
            int v = io.reg8(a, 0xD1);
            if (v == 0x5C) { set("ENV III pressure sensor", "Air pressure (QMP6988)."); break; }
            uint8_t z = 0; io.cmd(a, &z, 1);    // it was a PaHub after all: close the channels again
        }
        set("Unit PaHub", "I2C hub. MitamaOS looks behind each channel.");
        break;
    default:
        if (io.m5unit(a)) set("M5 Unit", "An M5Stack Unit MitamaOS doesn't know yet. Left alone.", D_M5FW);
        break;
    }
    return f;
}

// ── the scanner ────────────────────────────────────────────────────────
class Scanner {
public:
    static Scanner& get() { static Scanner s; return s; }

    const Result& result() const { return _r; }

    // Internal-bus chips that belong to the Cardputer-Adv or the LoRa cap.
    static bool builtIn(uint8_t a) { return a == 0x18 || a == 0x34 || a == 0x43 || a == 0x68; }

    // The Grove pin a GPS Unit was found on and saved for the GPS, or -1.
    static int gpsPin() {
        Preferences p; p.begin("tsukumo", true); int g = p.getUChar("gpspin", 0);   // 0 = none p.end();
        return g == G_SDA || g == G_SCL ? g : -1;
    }
    bool gpsOnGrove() { return gpsPin() >= 0; }
    void setGpsOnGrove(bool on, int pin) {
        Preferences p; p.begin("tsukumo", false); p.putUChar("gpspin", on ? pin : 0); p.end();
        SdSettings::changed();
        _r.gpsGrove = on;
    }

    void scan() {
        if (monsterLinked) {         // the board is in use: keep its row, touch nothing
            Found m = _r.dev[0];
            _r = Result(); _r.scanned = true; _r.monster = true; _add(m);
            _r.capLora = m5::In_I2C.scanID(0x43, 400000);
            _scanCap();
            return;
        }
        _r = Result();
        _r.scanned = true;
        _r.capLora = m5::In_I2C.scanID(0x43, 400000);
        _r.gpsGrove = gpsOnGrove();
        if (!_r.gpsGrove && _linesOk()) {
            // Something driving G1 high may be a serial board's TX. Ask
            // before any I2C, so the scan never pulls against it.
            if (_g1Driven && pingMonster()) {
                _r.monster = true;
                _add({ UART_ADDR, B_GROVE, 0, 0, D_MONSTER, "M5MonsterC5",
                       "ESP32-C5 board with 5 GHz Wi-Fi. MitamaOS uses only its listening commands: "
                       "5 GHz scans for Scry and its deauth detector. Never its attacks." });
            } else _scanGrove();
        }
        _scanCap();
    }

    // M5MonsterC5 (JanOS firmware): a UART on Grove, its TX on G1 and RX
    // on G2, 115200 8N1. "ping" gets "pong" back. That's the only command
    // MitamaOS sends it. Takes up to 150 ms.
    bool pingMonster(uint32_t* ms = nullptr) {
        Serial2.begin(115200, SERIAL_8N1, G_SCL, G_SDA);
        while (Serial2.available()) Serial2.read();
        uint32_t t0 = millis();
        Serial2.print("ping\r\n");
        Serial2.flush();
        char line[24]; int n = 0; bool ok = false;
        while (!ok && millis() - t0 < 150) {
            while (Serial2.available()) {
                char c = Serial2.read();
                if (c == '\n' || c == '\r') { line[n] = 0; if (strstr(line, "pong")) { ok = true; break; } n = 0; }
                else if (n < (int)sizeof(line) - 1) line[n++] = c;
            }
            delay(2);
        }
        if (ms) *ms = millis() - t0;
        Serial2.end();
        pinMode(G_SDA, INPUT); pinMode(G_SCL, INPUT);
        return ok;
    }

    // Listens on the Grove pins for a serial Unit that talks first (GPS,
    // QR scanner). Receive-only. Takes about 3 s.
    void listenUart() {
        if (_r.n > 0 || _r.gpsGrove || _r.monster) return;   // something already answered
        static const uint32_t BAUD[2] = { 115200, 9600 };
        static const int PIN[2] = { G_SDA, G_SCL };
        for (int p = 0; p < 2; p++) for (int b = 0; b < 2; b++) {
            Serial2.begin(BAUD[b], SERIAL_8N1, PIN[p], -1);
            uint32_t t = millis(); uint16_t n = 0; char last = 0; bool nmea = false;
            while (millis() - t < 700) {
                while (Serial2.available()) { char c = Serial2.read(); n++; if (last == '$' && c == 'G') nmea = true; last = c; }
                delay(5);
            }
            Serial2.end();
            if (n > 8) {
                _r.uartPin = PIN[p]; _r.uartBaud = BAUD[b]; _r.uartBytes = n; _r.uartNmea = nmea;
                pinMode(PIN[p], INPUT);
                return;
            }
        }
        pinMode(G_SDA, INPUT); pinMode(G_SCL, INPUT);
    }

    // First identified Unit, for the Mitama's boot line.
    const char* firstName() const {
        for (int i = 0; i < _r.n; i++) if (strcmp(_r.dev[i].name, "Unknown device")) return _r.dev[i].name;
        // An unknown chip on the internal bus could be the board's own.
        for (int i = 0; i < _r.n; i++) if (_r.dev[i].bus != B_CAP) return "something unknown";
        return nullptr;
    }

private:
    Result _r;
    bool   _g1Driven = false;
    bool   _direct[128] = {};    // ACKs on the Grove port with every PaHub channel closed

    // With the pins as inputs: a line that reads low even with the pull-up
    // on is held by a plain-pin Unit, so I2C would only confuse it.
    bool _linesOk() {
        pinMode(G_SDA, INPUT_PULLUP); pinMode(G_SCL, INPUT_PULLUP);
        delayMicroseconds(50);
        bool up = digitalRead(G_SDA) && digitalRead(G_SCL);
        pinMode(G_SDA, INPUT_PULLDOWN); pinMode(G_SCL, INPUT_PULLDOWN);
        delayMicroseconds(50);
        _g1Driven = digitalRead(G_SCL);
        bool driven = digitalRead(G_SDA) && _g1Driven;
        pinMode(G_SDA, INPUT); pinMode(G_SCL, INPUT);
        _r.lineLow = !up;
        _r.linesHigh = driven;
        return up;
    }

    void _add(const Found& f) { if (_r.n < MAX_FOUND) _r.dev[_r.n++] = f; }

    void _scanBus(const Io& io, uint8_t bus, uint8_t ch, bool skipBuiltIn, uint8_t hub = 0) {
        bool seen[128] = {};
        for (uint8_t a = 0x08; a <= 0x77; a++) {
            if (skipBuiltIn && builtIn(a)) continue;
            if (bus == B_HUB && (a >= 0x70 || _direct[a])) continue;   // the hub, and what's outside it
            if (a == 0x54) continue;                     // Roller bootloader's reserved address
            seen[a] = io.ack(a);
        }
        if (bus == B_GROVE) memcpy(_direct, seen, sizeof(_direct));
        for (uint8_t a = 0x08; a <= 0x77; a++) if (seen[a]) {
            // The second chip of a two-chip Unit is named on its own row
            // only when it says something extra.
            Found f = identify(io, a, seen);
            f.bus = bus; f.hubCh = ch; f.hub = hub;
            _add(f);
        }
    }

    void _scanGrove() {
        auto& ex = m5::Ex_I2C;
        if (luaWire) { Wire.end(); luaWire = false; }
        if (!ex.begin()) return;
        Io io{ ex };
        _scanBus(io, B_GROVE, 0, false);
        // Behind a PaHub: open one channel at a time, close it after.
        for (int i = 0, n = _r.n; i < n; i++) {
            if (strcmp(_r.dev[i].name, "Unit PaHub")) continue;
            uint8_t hub = _r.dev[i].addr;
            for (uint8_t ch = 0; ch < 6; ch++) {
                uint8_t m = 1 << ch;
                if (!io.cmd(hub, &m, 1)) break;
                _scanBus(io, B_HUB, ch, false, hub);
            }
            uint8_t z = 0; io.cmd(hub, &z, 1);
        }
        ex.release();
        pinMode(G_SDA, INPUT); pinMode(G_SCL, INPUT);
    }

    // A Cap's Grove port is wired to the internal bus, next to the keyboard.
    void _scanCap() {
        Io io{ m5::In_I2C };
        _scanBus(io, B_CAP, 0, true);
    }
};

}  // namespace Tsukumo
