#pragma once
// WiGLE upload: sends a Scry CSV log to WiGLE's file-upload API
// (POST https://api.wigle.net/api/v2/file/upload, multipart "file" field,
// HTTP Basic auth with the account's API name and API token). The server
// certificate is checked against the Mozilla root list in ca_bundle.h.
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "../system/sd_settings.h"
#include <SD.h>
#include <base64.h>
#include "ca_bundle.h"

namespace Wigle {

struct Account { String name, token; bool ok() const { return name.length() && token.length(); } };

inline Account load() {
    Preferences p; p.begin("wigle", true);
    Account a{ p.getString("name", ""), p.getString("token", "") };
    p.end();
    return a;
}

inline void save(const Account& a) {
    Preferences p; p.begin("wigle", false);
    p.putString("name", a.name);
    p.putString("token", a.token);
    p.end();
    SdSettings::changed();
}

// Uploads one file. On failure `msg` says why in a few words.
inline bool upload(const char* path, const Account& acct, String& msg) {
    File f = SD.open(path, FILE_READ);
    if (!f) { msg = "Can't open log"; return false; }
    const char* fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;

    const char* B = "----MitamaOSScryBoundary7d1";
    String head = String("--") + B + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"" + fname + "\"\r\n"
        "Content-Type: text/csv\r\n\r\n";
    String tail = String("\r\n--") + B + "--\r\n";
    size_t total = head.length() + f.size() + tail.length();

    WiFiClientSecure c;
    c.setCACertBundle(CA_BUNDLE);
    c.setTimeout(20);
    if (!c.connect("api.wigle.net", 443)) { f.close(); msg = "Can't reach WiGLE"; return false; }

    String auth = base64::encode(acct.name + ":" + acct.token);
    c.print(String("POST /api/v2/file/upload HTTP/1.1\r\n"
                   "Host: api.wigle.net\r\n"
                   "User-Agent: MitamaOS-Scry\r\n"
                   "Accept: application/json\r\n"
                   "Authorization: Basic ") + auth + "\r\n"
            "Content-Type: multipart/form-data; boundary=" + B + "\r\n"
            "Content-Length: " + String((unsigned)total) + "\r\n"
            "Connection: close\r\n\r\n");
    c.print(head);
    uint8_t buf[1024];
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        if (c.write(buf, n) != (size_t)n) { f.close(); c.stop(); msg = "Upload cut off"; return false; }
    }
    f.close();
    c.print(tail);

    // Status line, then skim the body for WiGLE's success flag.
    String status = c.readStringUntil('\n');
    int code = status.length() > 12 ? status.substring(9, 12).toInt() : 0;
    String body;
    uint32_t t0 = millis();
    while ((c.connected() || c.available()) && millis() - t0 < 15000 && body.length() < 2048) {
        if (c.available()) body += (char)c.read(); else delay(5);
    }
    c.stop();
    if (code == 401 || code == 403) { msg = "WiGLE rejected the API name/token"; return false; }
    if (code != 200 || body.indexOf("\"success\":true") < 0) {
        msg = "WiGLE error " + String(code);
        return false;
    }
    msg = "Uploaded";
    return true;
}

} // namespace Wigle
