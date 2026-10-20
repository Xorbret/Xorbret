#pragma once
// Update fetcher: asks the MitamaOS Pages site which build is newest and
// downloads that build's merged image to the SD card, where Launcher can
// install it (Launcher > SD). MitamaOS can't overwrite itself while it
// runs, and Launcher keeps its own flash layout, so the last step stays
// in Launcher.
//
// https://xorbret.github.io/Xorbret/firmware/version.txt holds the build
// id (short git hash) of the newest build; the image sits next to it.
// TLS is checked against the Mozilla roots in ca_bundle.h.
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <SD.h>
#include "ca_bundle.h"

#ifndef MITAMA_BUILD
#define MITAMA_BUILD "dev"
#endif

namespace OsUpdate {

static constexpr const char* HOST = "xorbret.github.io";
static constexpr const char* BASE = "/Xorbret/firmware/";
static constexpr const char* IMAGE = "mitamaos-cardputer-adv-merged.bin";
static constexpr const char* DIR = "/MitamaOS/update";

inline const char* current() { return MITAMA_BUILD; }

// Sends a GET and reads the headers. Returns the body length (-1 if the
// server didn't say), or -2 on failure with `msg` set.
inline long _get(WiFiClientSecure& c, const String& path, String& msg) {
    c.setCACertBundle(CA_BUNDLE);
    c.setTimeout(15);
    if (!c.connect(HOST, 443)) { msg = "Can't reach the update site"; return -2; }
    c.print(String("GET ") + path + " HTTP/1.1\r\nHost: " + HOST +
            "\r\nUser-Agent: MitamaOS\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n");
    String status = c.readStringUntil('\n');
    if (status.indexOf(" 200") < 0) {
        status.trim();
        msg = "Update site said: " + status.substring(status.indexOf(' ') + 1);
        c.stop(); return -2;
    }
    long len = -1;
    for (;;) {
        String h = c.readStringUntil('\n');
        h.trim();
        if (!h.length()) break;
        if (h.substring(0, 15).equalsIgnoreCase("Content-Length:")) len = h.substring(15).toInt();
    }
    return len;
}

// Newest build id, or "" with `msg` set.
inline String latest(String& msg) {
    WiFiClientSecure c;
    long len = _get(c, String(BASE) + "version.txt?t=" + millis(), msg);
    if (len == -2) return "";
    String v = c.readString();
    c.stop();
    v.trim();
    int nl = v.indexOf('\n');
    if (nl > 0) v = v.substring(0, nl);
    if (!v.length()) msg = "No version on the update site";
    return v;
}

// Downloads the newest image to /MitamaOS/update/mitamaos-<build>.bin.
// progress(done, total) is called as it goes. Returns the path, or ""
// with `msg` set; a part-finished download is deleted.
inline String download(const String& build, void (*progress)(size_t, size_t), String& msg) {
    if (!SD.exists("/MitamaOS")) SD.mkdir("/MitamaOS");
    if (!SD.exists(DIR)) SD.mkdir(DIR);
    String path = String(DIR) + "/mitamaos-" + build + ".bin";
    WiFiClientSecure c;
    long len = _get(c, String(BASE) + IMAGE + "?t=" + millis(), msg);
    if (len == -2) return "";
    if (len <= 0) { c.stop(); msg = "Update size unknown"; return ""; }
    if (SD.totalBytes() - SD.usedBytes() < (uint64_t)len + 65536) { c.stop(); msg = "SD card is full"; return ""; }
    String tmp = path + ".part";
    SD.remove(tmp.c_str());
    File f = SD.open(tmp.c_str(), FILE_WRITE);
    if (!f) { c.stop(); msg = "Can't write to the SD card"; return ""; }
    uint8_t* buf = (uint8_t*)malloc(4096);
    if (!buf) { f.close(); c.stop(); msg = "Not enough memory"; return ""; }
    size_t done = 0;
    uint32_t last = millis();
    while (done < (size_t)len) {
        int n = c.read(buf, min((size_t)4096, (size_t)len - done));
        if (n > 0) {
            if (f.write(buf, n) != (size_t)n) { msg = "SD write failed"; break; }
            done += n;
            last = millis();
            if (progress) progress(done, len);
        } else if (!c.connected() || millis() - last > 20000) {
            msg = "Download interrupted"; break;
        } else {
            delay(5);
        }
    }
    free(buf);
    f.close();
    c.stop();
    if (done != (size_t)len) { SD.remove(tmp.c_str()); return ""; }
    SD.remove(path.c_str());
    SD.rename(tmp.c_str(), path.c_str());
    return path;
}

} // namespace OsUpdate
