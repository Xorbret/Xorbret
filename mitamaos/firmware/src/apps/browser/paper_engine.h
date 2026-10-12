#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SD.h>
#include "../../config.h"

#define PE_MAX_LINES   99
#define PE_LINE_WIDTH  40
#define PE_MAX_LINKS   100
#define PE_MAX_FIELDS   24   // <input>/<button> elements per page
#define PE_MAX_FORMS     6   // <form> elements per page
#define PE_MAX_SELECT  (PE_MAX_LINKS + PE_MAX_FIELDS)  // links+fields, in doc order
#define PE_MAX_IMAGES   16   // <img src> URLs collected per page (see imgURLs)

// --- Mini layout/CSS engine limits ---------------------------------------
// All bounded and fixed-size on purpose: this board has no PSRAM (see
// platformio.ini) and only ~200-300KB of free heap to share between WiFi,
// the display sprite, Lua, and everything else. A real DOM + full CSS
// cascade would need dynamically-sized trees this hardware can't spare
// the RAM for - these caps make the "reduced" in "a reduced version of a
// real parser" precise: past them, a page is still readable, it just stops
// gaining new style/structure information.
#define PE_MAX_CSS_RULES    16   // simple selectors kept from all <style> blocks combined
#define PE_MAX_TAG_DEPTH    24   // element-nesting depth the style stack tracks
#define PE_MAX_LIST_DEPTH    6   // nested <ul>/<ol> depth
#define PE_CSS_BUF_CAP      400  // raw <style> text considered, per page
#define PE_SCRIPT_BUF_CAP   400  // raw <script> text considered per script tag,
                                  // when scanning for a JS-redirect pattern
                                  // (both kept modest on purpose: Arduino's
                                  // String doesn't shrink its buffer back
                                  // down after growing, so whatever these
                                  // grow to on the first page that uses
                                  // them stays reserved, unused, for the
                                  // rest of the session - see paper_engine.h)

// Per-line render flags (returned via onLineReady's 3rd argument, stored by
// the browser alongside each line of text) - whole-line granularity only;
// see _currentLineFlags() for why headings/alignment work this way while
// bold/color (see the \x05.. \x08 markers below) work per run instead.
#define PE_LF_BIG      0x01   // render at 2x text size (h1/h2)
#define PE_LF_HEADING  0x02   // any heading (h1/h2/h3) - used to build the page outline
#define PE_LF_CENTER   0x04   // center this line horizontally

enum ParseState { PS_TEXT, PS_TAG, PS_SCRIPT, PS_STYLE, PS_CSS, PS_ENTITY, PS_TITLE, PS_TITLE_TAG };

class PaperEngine {
public:
    int  bytesDownloaded = 0;
    int  totalBytes      = 0;
    int  lineCount       = 0;

    // HTTP status of the last fetch(): 200/301/404/etc from the server,
    // or a negative HTTPClient library error (connection refused, timeout...).
    // 0 means fetch() was never called.
    int    lastHttpCode = 0;
    String pageTitle    = "";  // from <title>, empty if page had none

    String linkURLs[PE_MAX_LINKS];
    int    linkCount = 0;

    // --- Forms & input fields --------------------------------------------
    // A tiny subset of HTML forms: text-like <input> elements (and their
    // submit button) become on-screen, selectable fields the same way links
    // are. Checkboxes/radios/file inputs are intentionally not supported -
    // there's no realistic way to represent multi-state controls on this
    // display/input combo, so they're silently skipped rather than shown
    // half-working.
    struct FormInfo {
        String action;      // resolved relative to the page URL when submitted
        bool   isGet = true; // false = <form method="post"> (submitted as GET anyway - see browser.h)
    };
    struct FieldInfo {
        String name;
        String value;
        bool   isSubmit = false;  // type="submit"/"button"/"image"
        int    formId    = -1;    // index into forms[], or -1 if outside any <form>
    };
    FormInfo  forms[PE_MAX_FORMS];
    int       formCount = 0;
    FieldInfo fields[PE_MAX_FIELDS];
    int       fieldCount = 0;

    // Links and fields in the order they actually appear in the document,
    // so ,/ navigation moves through the page the way it looks on screen
    // instead of "all links, then all fields".
    enum SelKind { EK_LINK, EK_FIELD };
    struct SelectableInfo { SelKind kind; int idx; };
    SelectableInfo selectables[PE_MAX_SELECT];
    int            selectableCount = 0;

    // <img src="..."> URLs collected while parsing, in document order - not
    // decoded/rendered inline (no room in the text-mode layout for that),
    // but exposed so the browser can offer a "view images on this page"
    // list and hand a chosen one off to the File Manager's real image
    // viewer (see Browser::_downloadAndViewImage).
    String imgURLs[PE_MAX_IMAGES];
    int    imgCount = 0;

    // <meta http-equiv="refresh" content="N;url=..."> support. Only the
    // first such tag on a page is honored (matches real-browser behavior);
    // metaRefreshURL is empty if the tag was a bare "refresh after N
    // seconds, same page" with no url= part, in which case the browser
    // won't act on it (no realistic use for a client-side self-timer here).
    bool   metaRefreshFound = false;
    String metaRefreshURL   = "";
    int    metaRefreshSecs  = 0;
    // True if metaRefreshURL came from a JS redirect pattern (see
    // _scanJsRedirect) rather than a real <meta http-equiv="refresh">, so
    // the browser can be honest about it in the "Page redirects..." toast
    // instead of implying it read an actual meta tag.
    bool   metaRefreshFromJS = false;

    void (*onLineReady)(String& line, int lineNum, uint8_t lineFlags) = nullptr;

    String sessionCookie = "";

    // Maps an HTTP status (or negative HTTPClient error) to a color family -
    // 2xx success, 3xx redirect/warn, 4xx/5xx/connection error - so the UI
    // can show state at a glance instead of just "error / not error".
    static uint16_t codeColor(int code) {
        if (code <= 0)                  return COL_ERR;   // connection-level error
        if (code >= 200 && code < 300)  return COL_OK;
        if (code >= 300 && code < 400)  return COL_WARN;
        return COL_ERR;                                    // 4xx/5xx
    }

    // Small fixed on-screen palette a <style>/style="" color: value gets
    // bucketed into (see _paletteIdFor) - not real color accuracy, just
    // enough to tell "this span is meant to stand out" apart from body
    // text, which is what a page author using color almost always means.
    // id 4 ("dim") isn't reachable from CSS - it's reused internally for
    // blockquote/pre/code.
    static uint16_t paletteColor(int id) {
        switch (id) {
            case 0: return COL_ERR;    // red family
            case 1: return COL_OK;     // green family
            case 2: return 0x027F;     // blue - deliberately not COL_ACCENT,
                                        // which is already the link color
            case 3: return COL_WARN;   // orange/gold family
            case 4: return COL_DIM;    // blockquote / pre / code
            default: return COL_TEXT;
        }
    }

    // Human-readable text for an HTTP status or HTTPClient error code,
    // so the browser can show "Error 404: Not Found" instead of nothing.
    static String httpCodeText(int code) {
        switch (code) {
            case 200: return "OK";
            case 201: return "Created";
            case 202: return "Accepted (likely bot-check/challenge page)";
            case 204: return "No Content";
            case 301: return "Moved Permanently";
            case 302: return "Found (redirect)";
            case 303: return "See Other";
            case 304: return "Not Modified";
            case 307: return "Temporary Redirect";
            case 308: return "Permanent Redirect";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 408: return "Request Timeout";
            case 429: return "Too Many Requests";
            case 500: return "Internal Server Error";
            case 502: return "Bad Gateway";
            case 503: return "Service Unavailable";
            case 504: return "Gateway Timeout";
            case HTTPC_ERROR_CONNECTION_REFUSED:  return "Connection refused";
            case HTTPC_ERROR_SEND_HEADER_FAILED:   return "Send header failed";
            case HTTPC_ERROR_SEND_PAYLOAD_FAILED:  return "Send payload failed";
            case HTTPC_ERROR_NOT_CONNECTED:        return "Not connected";
            case HTTPC_ERROR_CONNECTION_LOST:      return "Connection lost";
            case HTTPC_ERROR_NO_STREAM:            return "No stream";
            case HTTPC_ERROR_NO_HTTP_SERVER:       return "No HTTP server";
            case HTTPC_ERROR_TOO_LESS_RAM:         return "Not enough RAM";
            case HTTPC_ERROR_ENCODING:             return "Bad encoding";
            case HTTPC_ERROR_STREAM_WRITE:         return "Stream write error";
            case HTTPC_ERROR_READ_TIMEOUT:         return "Connection timed out";
            default:
                if (code < 0)   return "Connection error";
                if (code >= 300 && code < 400) return "Redirect (not followed)";
                if (code >= 400 && code < 500) return "Client error";
                if (code >= 500)               return "Server error";
                return "Unknown error";
        }
    }

    void reset() {
        _line      = "";
        lineCount  = 0;
        linkCount  = 0;
        fieldCount = 0;
        formCount  = 0;
        selectableCount = 0;
        imgCount   = 0;
        _curForm      = -1;
        _activeLinkId = -1;
        _state     = PS_TEXT;
        _tag       = "";
        _entity    = "";
        _endTag    = "";
        _lastSpace = false;
        _titleBuf  = "";
        pageTitle  = "";
        metaRefreshFound = false;
        metaRefreshURL   = "";
        metaRefreshSecs  = 0;
        metaRefreshFromJS = false;
        _scriptBuf = "";

        // Mini CSS/layout engine state (see PE_MAX_* constants above)
        _cssBuf       = "";
        _cssRuleCount = 0;
        _tagDepth     = 0;
        _boldDepth    = 0;
        _centerDepth  = 0;
        _headingActive = false;
        _headingBig    = false;
        _listDepth     = 0;
        // Without these two, a page whose fetch got cut short mid-<p> (the
        // ESP.getFreeHeap() < 15000 guard in _fetchPlain can do exactly
        // that under memory pressure) would leave _blockStyleBold/ColorId
        // "on" while _boldDepth above just got force-reset to 0 - the next
        // page's first p/li/td/th/tr would then see _blockStyleBold still
        // true and spuriously emit a bold-off marker before anything was
        // ever turned on. Harmless to render, but worth not doing.
        _blockStyleBold    = false;
        _blockStyleColorId = -1;
        _blockStyleCenter  = false;
    }

    bool fetch(const String& url,
               const String& referer = "",
               int timeoutMs = 60000,
               void (*progress)(int, int) = nullptr)
    {
        // Redirects are followed by hand, one hop at a time, instead of via
        // HTTPClient's own setFollowRedirects(). Reason: we pass an explicit
        // WiFiClient/WiFiClientSecure into http.begin(client, url) so we can
        // pick plain vs TLS per-request (see below) - but once you hand
        // HTTPClient an explicit client object, it keeps using that same
        // object for any redirect it follows internally, even if the
        // Location: header crosses from http:// to https:// or back. A
        // WiFiClientSecure doing a raw (non-TLS) HTTP exchange, or a plain
        // WiFiClient suddenly fed a TLS handshake, both fail - which is
        // exactly the "works on https, breaks on http" pattern this fixes.
        String curURL = url;
        int    code   = 0;

        for (int hop = 0; hop <= 5; hop++) {
            // Real fix for the -1 / -11 "random" failures on plain http://
            // sites (example.com, httpbin.org, ...): the old code always
            // used WiFiClientSecure, which always performs a TLS handshake
            // regardless of scheme. Against a server that only speaks plain
            // HTTP on that port, that handshake either gets reset instantly
            // (HTTPC_ERROR_CONNECTION_REFUSED, -1) or hangs until timeout
            // (HTTPC_ERROR_READ_TIMEOUT, -11), depending on how that
            // particular server/proxy reacts to receiving TLS bytes on a
            // plain port. Picking the transport by scheme avoids both.
            bool isHttps = curURL.startsWith("https://");

            WiFiClientSecure secureClient;
            WiFiClient       plainClient;
            if (isHttps) secureClient.setInsecure();
            WiFiClient& client = isHttps ? static_cast<WiFiClient&>(secureClient)
                                          : plainClient;
            client.setTimeout(timeoutMs / 1000);

            HTTPClient http;
            http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);  // we do it ourselves, see above
            http.setTimeout(timeoutMs);

            if (!http.begin(client, curURL)) { lastHttpCode = code ? code : -1; return false; }

            const char* hdrKeys[] = { "Set-Cookie", "Content-Encoding", "Location" };
            http.collectHeaders(hdrKeys, 3);

            // A custom UA token (the old "...PaperOS/1.0" suffix) and missing
            // Accept/Accept-Language headers are exactly what bot-detection on
            // sites like DuckDuckGo flags (hence the 202 "please wait" replies).
            // This mimics a real mobile Chrome request as closely as this
            // HTTP client allows.
            http.addHeader("Accept-Encoding", "identity");
            http.addHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
            http.addHeader("Accept-Language", "en-US,en;q=0.9");
            http.addHeader("User-Agent",
                "Mozilla/5.0 (Linux; Android 10; Mobile) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36");
            if (sessionCookie != "")
                http.addHeader("Cookie", sessionCookie);
            // Real browsers keep sending the *original* referring page across
            // a redirect chain, not the intermediate redirect URL, so this
            // stays the same on every hop.
            if (referer.length() > 0)
                http.addHeader("Referer", referer);

            code = http.GET();
            if (http.hasHeader("Set-Cookie"))
                sessionCookie = http.header("Set-Cookie");

            bool isRedirect = (code >= 300 && code < 400) && http.hasHeader("Location");
            if (!isRedirect) {
                lastHttpCode = code;
                if (code != 200) { http.end(); return false; }

                totalBytes = http.getSize();
                if (totalBytes <= 0) totalBytes = 1;

                reset();

                WiFiClient* stream = http.getStreamPtr();
                bytesDownloaded = 0;
                uint32_t t0 = millis();

                _fetchPlain(stream, http, timeoutMs, t0, progress);

                _flushLine();
                http.end();
                return lineCount > 0;
            }

            String location = http.header("Location");
            http.end();

            String next = _resolveURL(curURL, location);
            if (next.length() == 0 || next == curURL) { lastHttpCode = code; return false; }
            curURL = next;
        }

        lastHttpCode = code;  // too many redirects - report the last hop's code
        return false;
    }

    // Downloads a URL's raw bytes straight to an SD file, following
    // redirects and picking http/https transport the same way fetch()
    // does above - but with no HTML parsing at all. Used to save an
    // <img> the page linked to so the File Manager's own BMP/PNG/JPG
    // viewer can open it (this engine has no image decoder of its own,
    // and doesn't need one - the OS already has one, see browser.h).
    bool fetchToFile(const String& url, const String& referer, const String& savePath,
                      int timeoutMs = 20000, void (*progress)(int, int) = nullptr)
    {
        String curURL = url;
        int    code   = 0;

        for (int hop = 0; hop <= 5; hop++) {
            bool isHttps = curURL.startsWith("https://");

            WiFiClientSecure secureClient;
            WiFiClient       plainClient;
            if (isHttps) secureClient.setInsecure();
            WiFiClient& client = isHttps ? static_cast<WiFiClient&>(secureClient)
                                          : plainClient;
            client.setTimeout(timeoutMs / 1000);

            HTTPClient http;
            http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
            http.setTimeout(timeoutMs);
            if (!http.begin(client, curURL)) { lastHttpCode = code ? code : -1; return false; }

            const char* hdrKeys[] = { "Location" };
            http.collectHeaders(hdrKeys, 1);
            http.addHeader("Accept", "*/*");
            http.addHeader("User-Agent",
                "Mozilla/5.0 (Linux; Android 10; Mobile) AppleWebKit/537.36 "
                "(KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36");
            if (referer.length() > 0) http.addHeader("Referer", referer);

            code = http.GET();
            bool isRedirect = (code >= 300 && code < 400) && http.hasHeader("Location");

            if (!isRedirect) {
                lastHttpCode = code;
                if (code != 200) { http.end(); return false; }

                File f = SD.open(savePath.c_str(), FILE_WRITE);
                if (!f) { http.end(); return false; }

                int total = http.getSize(); if (total <= 0) total = 1;
                WiFiClient* stream = http.getStreamPtr();
                uint8_t buf[512];
                int dl = 0, noData = 0;
                uint32_t t0 = millis();
                while (http.connected() && millis() - t0 < (uint32_t)timeoutMs) {
                    if (ESP.getFreeHeap() < 15000) break;
                    int avail = stream->available();
                    if (avail > 0) {
                        noData = 0;
                        int n = stream->read(buf, min((int)sizeof(buf), avail));
                        if (n > 0) {
                            f.write(buf, n);
                            dl += n;
                            if (progress) progress(dl, total);
                        }
                    } else {
                        if (++noData > 1000) break;
                        delay(1);
                    }
                }
                f.close();
                http.end();
                return dl > 0;
            }

            String location = http.header("Location");
            http.end();

            String next = _resolveURL(curURL, location);
            if (next.length() == 0 || next == curURL) { lastHttpCode = code; return false; }
            curURL = next;
        }

        lastHttpCode = code;
        return false;
    }

private:
    ParseState _state     = PS_TEXT;
    String     _line      = "";
    String     _tag       = "";
    String     _entity    = "";
    String     _endTag    = "";
    bool       _lastSpace = false;
    String     _titleBuf  = "";

    // Id of the <a> we're currently inside, or -1. Real pages sometimes wrap
    // block elements inside a link, or the link text is simply long enough
    // to word-wrap - either way the color marker byte was only written once,
    // at the *start* of the link, so a line break (from either _flushLine()
    // or the width-based wrap in _addChar()) would otherwise leave the
    // continuation rendered as plain unhighlighted text even though it's
    // still part of the link. Re-emitting the marker at the top of each new
    // line while this is set fixes that.
    int _activeLinkId = -1;

    // Index into forms[] while inside a <form>...</form>, else -1.
    int _curForm = -1;

    // --- Mini CSS/layout engine -------------------------------------------
    // A genuinely small, real (not faked) CSS engine: <style> blocks are
    // parsed into simple tag/.class/#id selectors with a handful of
    // supported properties, and applied via a real element-nesting stack
    // as tags open/close - not string-matched after the fact. What it
    // deliberately does NOT do: combinators/descendant selectors,
    // specificity beyond tag<class<id<inline, !important, or any property
    // besides color/font-weight/text-align. Those would need a real DOM to
    // do properly, which doesn't fit this hardware's RAM (see PE_MAX_*).

    struct CssStyle {
        bool   bold      = false;
        bool   center    = false;
        bool   hasColor  = false;
        int8_t colorId   = -1;   // see paletteColor()
    };

    struct CssRule {
        char     kind;   // 't' = tag, 'c' = class, 'i' = id
        String   name;
        CssStyle style;
    };
    CssRule _cssRules[PE_MAX_CSS_RULES];
    int     _cssRuleCount = 0;
    String  _cssBuf = "";  // raw text of the current <style>...</style>, capped at PE_CSS_BUF_CAP
    String  _scriptBuf = "";  // raw text of the current <script>...</script>, capped at PE_SCRIPT_BUF_CAP

    // Looks for the handful of ways real pages redirect via JS instead of
    // <meta refresh> - location.href = "...", location.replace("..."),
    // location.assign("..."), window.location = "...", top.location = "...".
    // Deliberately just pattern-matching text, not executing anything: a
    // page that computes its redirect target at runtime (e.g. from a
    // cookie or query param) won't be caught, only literal string targets -
    // but that covers a large share of real-world "this page is just a
    // redirect stub" JS in practice.
    static String _scanJsRedirect(const String& jsRaw) {
        String jsLower = jsRaw; jsLower.toLowerCase();  // search case-insensitively,
                                                          // extract from the original (URLs are case-sensitive)
        static const char* PATTERNS[] = {
            "location.href", "location.replace(", "location.assign(",
            "location=", "top.location"
        };
        for (const char* pat : PATTERNS) {
            int idx = jsLower.indexOf(pat);
            if (idx < 0) continue;
            int from = idx + strlen(pat);
            int q1 = jsRaw.indexOf('"', from);
            int q1b = jsRaw.indexOf('\'', from);
            if (q1 < 0 || (q1b >= 0 && q1b < q1)) q1 = q1b;
            if (q1 < 0) continue;
            char qc = jsRaw[q1];
            int q2 = jsRaw.indexOf(qc, q1 + 1);
            if (q2 < 0) continue;
            String url = jsRaw.substring(q1 + 1, q2);
            url.trim();
            if (url.length() > 0) return url;
        }
        return "";
    }

    // One frame per currently-open element, so a closing tag can reverse
    // exactly what its own opening tag applied - real nesting, not a flat
    // on/off toggle that a page with several nested spans would desync.
    struct TagFrame {
        String name;
        bool   pushedBold   = false;
        bool   pushedColor  = false;
        bool   pushedCenter = false;
    };
    TagFrame _tagStack[PE_MAX_TAG_DEPTH];
    int      _tagDepth = 0;

    int  _boldDepth   = 0;  // >0 while inside any bold-resolved element (nesting-safe)
    int  _centerDepth = 0;  // >0 while inside any center-resolved element

    // Headings get their own whole-line size instead of a per-character
    // marker (see PE_LF_BIG's comment on _currentLineFlags below).
    bool _headingActive = false;
    bool _headingBig    = false;

    // <ul>/<ol> nesting - real indent-by-depth and real item numbering for
    // <ol>, not a flat " * " bullet regardless of context.
    bool _listIsOrdered[PE_MAX_LIST_DEPTH];
    int  _listCounter[PE_MAX_LIST_DEPTH];
    int  _listDepth = 0;

    // What PE_LF_* flags the line being built right now should carry, read
    // fresh from live state at every flush - so there's nothing to forget to
    // reset if a page's tags are unbalanced (a very common real-world case).
    uint8_t _currentLineFlags() const {
        uint8_t f = 0;
        if (_headingActive) { f |= PE_LF_HEADING; if (_headingBig) f |= PE_LF_BIG; }
        if (_centerDepth > 0) f |= PE_LF_CENTER;
        return f;
    }

    void _mergeStyle(CssStyle& dst, const CssStyle& src) {
        if (src.bold)     dst.bold = true;
        if (src.center)   dst.center = true;
        if (src.hasColor) { dst.hasColor = true; dst.colorId = src.colorId; }
    }

    // Buckets a CSS color keyword or #hex into one of the 4 palette slots
    // (see paletteColor()) - not color-accurate, just enough to preserve
    // "the author wanted this to stand out, and roughly how".
    static int8_t _paletteIdFor(const String& val) {
        if (val.indexOf("red") >= 0 || val.indexOf("crimson") >= 0) return 0;
        if (val.indexOf("green") >= 0)  return 1;
        if (val.indexOf("blue") >= 0 || val.indexOf("navy") >= 0)  return 2;
        if (val.indexOf("orange") >= 0 || val.indexOf("gold") >= 0 ||
            val.indexOf("amber")  >= 0) return 3;
        if (val.startsWith("#") && val.length() >= 7) {
            long r = strtol(val.substring(1, 3).c_str(), NULL, 16);
            long g = strtol(val.substring(3, 5).c_str(), NULL, 16);
            long b = strtol(val.substring(5, 7).c_str(), NULL, 16);
            if (r > g + 40 && r > b + 40) return 0;
            if (g > r + 40 && g > b + 40) return 1;
            if (b > r + 40 && b > g + 40) return 2;
        }
        return -1;  // unrecognized / not worth a color change (e.g. black, gray, inherit)
    }

    // Parses one declaration block's-worth of "prop:value;prop:value" -
    // shared by <style> rule bodies and inline style="" attributes (the
    // real CSS cascade's two closest analogues on this scale).
    static CssStyle _parseDeclBlock(const String& body) {
        CssStyle st;
        int pos = 0;
        while (pos < (int)body.length()) {
            int semi = body.indexOf(';', pos);
            String decl = (semi < 0) ? body.substring(pos) : body.substring(pos, semi);
            pos = (semi < 0) ? body.length() : semi + 1;
            int colon = decl.indexOf(':');
            if (colon < 0) continue;
            String prop = decl.substring(0, colon); prop.trim(); prop.toLowerCase();
            String val  = decl.substring(colon + 1); val.trim(); val.toLowerCase();

            if (prop == "font-weight") {
                if (val == "bold" || val == "bolder" || val.toInt() >= 600) st.bold = true;
            } else if (prop == "text-align") {
                if (val == "center") st.center = true;
            } else if (prop == "color") {
                int8_t id = _paletteIdFor(val);
                if (id >= 0) { st.hasColor = true; st.colorId = id; }
            }
        }
        return st;
    }

    // Parses <style>...</style> body text into up to PE_MAX_CSS_RULES simple
    // rules. Only the first simple selector token of each rule is kept (no
    // "a, b {..}" lists, no "div p {..}" descendant combinators) - covers
    // the large majority of real single-class/tag "make this stand out"
    // rules while staying a single bounded pass with no recursion.
    void _parseCssRules(const String& css) {
        _cssRuleCount = 0;
        int pos = 0;
        while (pos < (int)css.length() && _cssRuleCount < PE_MAX_CSS_RULES) {
            int ob = css.indexOf('{', pos);
            if (ob < 0) break;
            int cb = css.indexOf('}', ob);
            if (cb < 0) break;
            String sel  = css.substring(pos, ob); sel.trim();
            String body = css.substring(ob + 1, cb);
            pos = cb + 1;

            int cut = sel.length();
            for (int i = 0; i < (int)sel.length(); i++) {
                char c = sel[i];
                if (c == ' ' || c == ',' || c == '>' || c == ':' || c == '\t' || c == '\n') { cut = i; break; }
            }
            sel = sel.substring(0, cut);
            if (sel.length() == 0) continue;

            char kind; String name;
            if (sel[0] == '.')      { kind = 'c'; name = sel.substring(1); }
            else if (sel[0] == '#') { kind = 'i'; name = sel.substring(1); }
            else                    { kind = 't'; name = sel; }
            name.toLowerCase();
            if (name.length() == 0) continue;

            CssStyle st = _parseDeclBlock(body);
            if (!st.bold && !st.center && !st.hasColor) continue;  // nothing we support in this rule

            _cssRules[_cssRuleCount].kind  = kind;
            _cssRules[_cssRuleCount].name  = name;
            _cssRules[_cssRuleCount].style = st;
            _cssRuleCount++;
        }
    }

    // Real (if simplified) cascade: built-in element defaults, then
    // <style> tag/class/id rules in that specificity order, then inline
    // style="" last (highest priority - matches real CSS, minus
    // !important). Properties are only ever turned ON by a later step,
    // never explicitly off again - so e.g. a stylesheet can't un-bold a
    // <th>. A real cascade needs a tri-state per property to do that; not
    // worth the extra RAM here for how rarely real pages rely on it.
    CssStyle _resolveStyle(const String& tagName, const String& rawTag) {
        CssStyle result;

        if (tagName == "b" || tagName == "strong" || tagName == "th") result.bold = true;
        if (tagName == "h1" || tagName == "h2" || tagName == "h3")    result.bold = true;
        if (tagName == "center")                                      result.center = true;

        // Cheap early-outs: this runs once per opened element, so on a
        // large real-world page (thousands of <div>/<span>/<a> tags) any
        // avoidable String work here adds up - the earlier version always
        // ran full _getAttr("class")/_getAttr("id") parses even on a page
        // with no <style> block at all (_cssRuleCount==0, so the results
        // could never match anything) and even on tags with no class/id/
        // style attribute to begin with. rawTag.indexOf() on the raw
        // attribute string is far cheaper than _getAttr's quote-parsing,
        // so use it to skip straight past tags/pages with nothing to gain.
        if (_cssRuleCount > 0) {
            for (int i = 0; i < _cssRuleCount; i++)
                if (_cssRules[i].kind == 't' && _cssRules[i].name == tagName)
                    _mergeStyle(result, _cssRules[i].style);

            if (rawTag.indexOf("class=") >= 0) {
                String cls = _getAttr(rawTag, "class");
                if (cls.length() > 0) {
                    cls.toLowerCase();
                    int p = 0;
                    while (p < (int)cls.length()) {
                        int sp = cls.indexOf(' ', p);
                        String one = (sp < 0) ? cls.substring(p) : cls.substring(p, sp);
                        p = (sp < 0) ? cls.length() : sp + 1;
                        if (one.length() == 0) continue;
                        for (int i = 0; i < _cssRuleCount; i++)
                            if (_cssRules[i].kind == 'c' && _cssRules[i].name == one)
                                _mergeStyle(result, _cssRules[i].style);
                    }
                }
            }

            if (rawTag.indexOf("id=") >= 0) {
                String id = _getAttr(rawTag, "id");
                if (id.length() > 0) {
                    id.toLowerCase();
                    for (int i = 0; i < _cssRuleCount; i++)
                        if (_cssRules[i].kind == 'i' && _cssRules[i].name == id)
                            _mergeStyle(result, _cssRules[i].style);
                }
            }
        }

        if (rawTag.indexOf("style=") >= 0) {
            String inlineStyle = _getAttr(rawTag, "style");
            if (inlineStyle.length() > 0) {
                inlineStyle.toLowerCase();
                _mergeStyle(result, _parseDeclBlock(inlineStyle));
            }
        }

        return result;
    }

    // Pushes one element's resolved style onto the real nesting stack and
    // emits the matching start marker(s) into the text stream - see the
    // marker legend above _handleTag().
    // HTML elements that never get a closing tag - pushing a style frame
    // for one and waiting for a "/tag" that will never arrive is exactly
    // the bug this exists to prevent (see the comment at its call site).
    // The trailing-slash check also catches XHTML/SVG-flavored explicit
    // self-closing syntax ("<div ... />") on any tag, not just this list.
    static bool _isVoidElement(const String& tagName, const String& rawTag) {
        if (tagName == "area" || tagName == "base"  || tagName == "br"    ||
            tagName == "col"  || tagName == "embed" || tagName == "hr"    ||
            tagName == "img"  || tagName == "input" || tagName == "link"  ||
            tagName == "meta" || tagName == "param" || tagName == "source"||
            tagName == "track"|| tagName == "wbr") return true;
        String t = rawTag; t.trim();
        return t.endsWith("/");
    }

    void _pushStyleFrame(const String& tagName, const String& rawTag) {
        if (_tagDepth >= PE_MAX_TAG_DEPTH) return;  // very deep markup - stop scoping further, content still renders
        CssStyle st = _resolveStyle(tagName, rawTag);
        TagFrame f; f.name = tagName;

        if (st.bold) {
            _boldDepth++;
            if (_boldDepth == 1) _addChar('\x05');
            f.pushedBold = true;
        }
        if (st.hasColor) {
            _addMarkerPair('\x07', (char)(st.colorId + 32));
            f.pushedColor = true;
        }
        if (st.center) { _centerDepth++; f.pushedCenter = true; }

        _tagStack[_tagDepth++] = f;
    }

    // Pops a frame only if it matches the tag actually closing - a
    // mismatched close (very common in real-world malformed HTML) is
    // silently ignored rather than risking a corrupted style stack.
    void _popStyleFrame(const String& tagName) {
        if (_tagDepth == 0 || _tagStack[_tagDepth - 1].name != tagName) return;
        TagFrame& f = _tagStack[_tagDepth - 1];
        if (f.pushedBold)  { _boldDepth = max(0, _boldDepth - 1); if (_boldDepth == 0) _addChar('\x06'); }
        if (f.pushedColor) { _addChar('\x08'); }
        if (f.pushedCenter) _centerDepth = max(0, _centerDepth - 1);
        _tagDepth--;
    }

    // p/li/td/th/tr are the second (bigger) half of the same problem the
    // void-element check above solves: HTML5 makes THEIR closing tags
    // optional too - a browser auto-closes a <p> the moment another block
    // element starts, but real pages constantly rely on that and never
    // write </p>/</li>/</td>/</th>/</tr> at all. p{color:...} is one of
    // the single most common real-world CSS rules there is, so this isn't
    // an edge case: pushing these onto the persistent open/close stack (as
    // an earlier version of this code did) meant almost any page styling
    // its paragraphs would get that color stuck on for the rest of the
    // page the first time it hit a <p> with no matching </p>. Instead,
    // these five apply their style directly and it's auto-reverted at the
    // next real block boundary - which unlike a specific closing tag, is a
    // bound every page reaches whether it explicitly closes its tags or not.
    bool   _blockStyleBold    = false;
    int8_t _blockStyleColorId = -1;
    bool   _blockStyleCenter  = false;

    void _closeBlockScopedStyle() {
        if (_blockStyleColorId >= 0) { _addChar('\x08'); _blockStyleColorId = -1; }
        if (_blockStyleBold) {
            _boldDepth = max(0, _boldDepth - 1);
            if (_boldDepth == 0) _addChar('\x06');
            _blockStyleBold = false;
        }
        if (_blockStyleCenter) { _centerDepth = max(0, _centerDepth - 1); _blockStyleCenter = false; }
    }

    void _openBlockScopedStyle(const String& tagName, const String& rawTag) {
        CssStyle st = _resolveStyle(tagName, rawTag);
        if (st.bold) { _boldDepth++; if (_boldDepth == 1) _addChar('\x05'); _blockStyleBold = true; }
        if (st.hasColor) { _addMarkerPair('\x07', (char)(st.colorId + 32)); _blockStyleColorId = st.colorId; }
        if (st.center) { _centerDepth++; _blockStyleCenter = true; }
    }

    // Resolves a possibly-relative redirect target (Location: header) against
    // the URL that was just fetched. Mirrors Browser::_resolveURL() in
    // browser.h (same rules for absolute / protocol-relative / rooted /
    // same-directory paths) - duplicated rather than shared because the
    // engine has no dependency on the Browser class, and this is small
    // enough that a shared header isn't worth the coupling.
    static String _resolveURL(const String& base, const String& rel) {
        if (rel.startsWith("http"))  return rel;
        if (rel.startsWith("//"))    return "https:" + rel;
        if (rel.startsWith("/")) {
            int p3 = base.indexOf("://");
            if (p3 < 0) return rel;
            int sl = base.indexOf('/', p3 + 3);
            if (sl < 0) return base + rel;
            return base.substring(0, sl) + rel;
        }
        int last = base.lastIndexOf('/');
        if (last > 7) return base.substring(0, last + 1) + rel;
        return base + "/" + rel;
    }

    // Generic quoted/unquoted attribute grab, e.g. _getAttr(tag, "href").
    // Case-sensitive on the attribute name (matches the existing href
    // handling below) - real-world markup is overwhelmingly lowercase here.
    static String _getAttr(const String& tag, const char* name) {
        String key = String(name) + "=";
        int idx = tag.indexOf(key);
        while (idx > 0 && tag[idx - 1] != ' ' && tag[idx - 1] != '\t' && tag[idx - 1] != '\n') {
            idx = tag.indexOf(key, idx + 1);
        }
        if (idx == -1) return "";
        int vstart = idx + key.length();
        if (vstart >= (int)tag.length()) return "";
        char q = tag[vstart];
        if (q == '"' || q == '\'') {
            int vend = tag.indexOf(q, vstart + 1);
            if (vend == -1) return "";
            return tag.substring(vstart + 1, vend);
        }
        int vend = vstart;
        while (vend < (int)tag.length() && tag[vend] != ' ' && tag[vend] != '>') vend++;
        return tag.substring(vstart, vend);
    }

    void _fetchPlain(WiFiClient* s, HTTPClient& h,
                     int tms, uint32_t t0, void (*prog)(int,int))
    {
        uint8_t buf[512];
        int noData = 0;
        while (h.connected() && millis() - t0 < (uint32_t)tms) {
            if (ESP.getFreeHeap() < 15000 || lineCount >= PE_MAX_LINES) break;
            int avail = s->available();
            if (avail > 0) {
                noData = 0;
                int n = s->read(buf, min((int)sizeof(buf), avail));
                if (n > 0) {
                    bytesDownloaded += n;
                    if (prog) prog(bytesDownloaded, totalBytes);
                    for (int i = 0; i < n && lineCount < PE_MAX_LINES; i++)
                        _processChar((char)buf[i]);
                }
            } else {
                if (++noData > 1000) break;
                delay(1);
            }
        }
    }

    void _processChar(char c) {
        switch (_state) {
        case PS_TEXT:
            if (c == '<') { _state = PS_TAG; _tag = ""; }
            else if (c == '&') { _state = PS_ENTITY; _entity = ""; }
            else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                if (!_lastSpace && _line.length() > 0) { _addChar(' '); _lastSpace = true; }
            } else { _addChar(c); _lastSpace = false; }
            break;

        case PS_TAG:
            if (c == '>') { _handleTag(); }
            else { if (_tag.length() < 180) _tag += c; }
            break;

        // Doesn't run any JS - there's no DOM here for a script to act on
        // (the page becomes a flat text stream, not an object tree), so a
        // real JS engine would have nothing to actually control on screen.
        // Instead: capture the script's text (bounded, like the CSS
        // capture below) and, on close, scan it for the single most common
        // pattern real pages use JS for that this browser can actually
        // act on - a client-side redirect (location.href = "..." and
        // friends). Same auto-follow path as <meta refresh>, see
        // metaRefreshFromJS below and Browser::_navigate().
        case PS_SCRIPT:
            if ((int)_scriptBuf.length() < PE_SCRIPT_BUF_CAP) _scriptBuf += c;
            _endTag += c;
            if (_endTag.length() > 9) _endTag.remove(0, 1);
            if (_endTag.equalsIgnoreCase("</script>")) {
                _endTag = ""; _state = PS_TEXT;
                if (!metaRefreshFound) {
                    String url = _scanJsRedirect(_scriptBuf);
                    if (url.length() > 0) {
                        metaRefreshFound   = true;
                        metaRefreshFromJS  = true;
                        metaRefreshSecs    = 0;   // JS redirects are immediate, no countdown to honor
                        metaRefreshURL     = url;
                    }
                }
                _scriptBuf = "";
            }
            break;

        case PS_STYLE:
            _endTag += c;
            if (_endTag.length() > 11) _endTag.remove(0, 1);
            if (_endTag.endsWith(">")) {
                String lo = _endTag; lo.toLowerCase();
                if (lo.endsWith("</style>") || lo.endsWith("</svg>") ||
                    lo.endsWith("</noscript>") || lo.endsWith("</head>")) {
                    _state = PS_TEXT; _endTag = "";
                }
            }
            break;

        // Actually captures <style>...</style> text (unlike PS_STYLE above,
        // which is used for svg/noscript and just discards everything) so
        // _parseCssRules() has something real to parse on </style>.
        case PS_CSS:
            if ((int)_cssBuf.length() < PE_CSS_BUF_CAP) _cssBuf += c;
            _endTag += c;
            if (_endTag.length() > 11) _endTag.remove(0, 1);
            if (_endTag.equalsIgnoreCase("</style>")) {
                int cut = _cssBuf.length() - 8;  // drop the "</style>" tail that leaked in above
                _cssBuf = (cut > 0) ? _cssBuf.substring(0, cut) : String("");
                _parseCssRules(_cssBuf);
                _cssBuf = ""; _endTag = ""; _state = PS_TEXT;
            }
            break;

        // Collects <title>...</title> text into pageTitle, separately from
        // the rendered body, so the browser can show a real page title.
        case PS_TITLE:
            if (c == '<') { _state = PS_TITLE_TAG; _endTag = "<"; }
            else if (_titleBuf.length() < 60) _titleBuf += c;
            break;

        case PS_TITLE_TAG:
            _endTag += c;
            if (c == '>') {
                if (_endTag.equalsIgnoreCase("</title>")) {
                    pageTitle = _titleBuf; pageTitle.trim();
                    _state = PS_TEXT;
                } else {
                    _state = PS_TITLE;  // some other tag inside <title>, ignore it
                }
                _endTag = "";
            }
            break;

        case PS_ENTITY:
            if (c == ';') { _decodeEntity(_entity); _state = PS_TEXT; }
            else if (c == ' ' || _entity.length() > 8) {
                _state = PS_TEXT; _addChar('&');
                for (int i = 0; i < (int)_entity.length(); i++) _addChar(_entity[i]);
                _addChar(c);
            } else { _entity += c; }
            break;
        }
    }

    // Marker legend for the escape bytes woven into _line / _dispLines
    // (all single control bytes, chosen to never collide with real
    // extended-ASCII/UTF-8 content this parser emits elsewhere):
    //   \x01 id \x02   link span        (existing)
    //   \x03 id \x04   form-field span   (existing)
    //   \x05 / \x06    bold on / off     (nesting-safe via _boldDepth)
    //   \x07 id \x08   color on / off    (id -> paletteColor(); "off" always
    //                                     resets to default, see _resolveStyle's
    //                                     doc comment on why nested colors of
    //                                     different ids don't restore each other)
    void _handleTag() {
        String base = _tag;
        int sp = base.indexOf(' ');
        if (sp != -1) base = base.substring(0, sp);
        base.toLowerCase();

        if (base == "script") { _state = PS_SCRIPT; _endTag = ""; return; }
        if (base == "style")  { _state = PS_CSS;   _endTag = ""; _cssBuf = ""; return; }
        if (base == "svg" || base == "noscript") {
            // Note: "head" is intentionally NOT swallowed whole anymore -
            // <title> lives inside it and needs to be reached individually.
            // Other head children (meta/link/base) carry no visible text,
            // so nothing extra leaks into the rendered page.
            _state = PS_STYLE; _endTag = "";
            return;
        }
        if (base == "title") { _state = PS_TITLE; _endTag = ""; _titleBuf = ""; return; }

        _state = PS_TEXT;
        bool isClose = base.startsWith("/");
        String tagName = isClose ? base.substring(1) : base;

        // Real (if small) CSS applies to every element, open or close -
        // EXCEPT void elements (br/img/meta/hr/input/...), which HTML never
        // gives a closing tag to. Pushing a frame for one of those and then
        // never popping it is exactly what happened before this fix: any
        // ordinary page has far more than PE_MAX_TAG_DEPTH void tags before
        // its <body> even starts (a dozen+ <meta>/<link> in <head> alone),
        // so the stack filled up almost immediately - after which no more
        // CSS tracking worked for the rest of the page, and worse, if any
        // void tag happened to match a bold/color rule, that style got
        // stuck ON for everything that followed, since its "frame" could
        // never be found and popped by a later closing tag. <input> is the
        // one void element with actual visible content worth coloring
        // (the "[Submit]" bracket text) - it's handled with its own
        // immediate wrap further down instead of the persistent stack.
        // Any real block boundary - a new p/div/tr/li/heading/list/table/
        // hr/blockquote/pre, OR one of those actually closing - is exactly
        // the point where a still-"open" p/li/td/th/tr style needs to end,
        // whether or not its own closing tag ever showed up (see
        // _closeBlockScopedStyle's comment). Checked by tagName alone since
        // that's already had any leading "/" stripped, so this one check
        // covers both the open and close spelling of each.
        static const char* BLOCK_BOUNDARIES[] = {
            "p","div","tr","td","th","table","hr","ul","ol","li",
            "blockquote","pre","code","h1","h2","h3"
        };
        for (const char* b : BLOCK_BOUNDARIES) {
            if (tagName == b) { _closeBlockScopedStyle(); break; }
        }

        bool isVoid = _isVoidElement(tagName, _tag);
        bool isOptionalClose = (tagName == "p" || tagName == "li" || tagName == "td" ||
                                 tagName == "th" || tagName == "tr");
        if (!isClose && !isVoid && !isOptionalClose) _pushStyleFrame(tagName, _tag);

        if (base == "a") {
            int hIdx = _tag.indexOf("href=");
            if (hIdx != -1) {
                int q = _tag.indexOf('"', hIdx);
                if (q == -1) q = _tag.indexOf('\'', hIdx);
                if (q != -1) {
                    int qe = _tag.indexOf(_tag[q], q + 1);
                    if (qe != -1 && linkCount < PE_MAX_LINKS && selectableCount < PE_MAX_SELECT) {
                        linkURLs[linkCount] = _tag.substring(q + 1, qe);
                        _addMarkerPair('\x01', (char)(linkCount + 32));
                        _activeLinkId = linkCount;
                        selectables[selectableCount++] = { EK_LINK, linkCount };
                        linkCount++;
                    }
                }
            }
        } else if (base == "/a") {
            _addChar('\x02');
            _activeLinkId = -1;
        }
        else if (base == "br" || base == "p" || base == "div") { _flushLine(); }
        else if (base == "tr") { _flushLine(); }
        else if (base == "td" || base == "th") {
            // Real table layout (column widths, borders) isn't realistic on
            // a 240px text-mode screen - this at least keeps cells visually
            // separated instead of running straight into each other as one
            // wall of prose, which is what happened before.
            if ((int)_line.length() > 0) { _addChar(' '); _addChar('|'); _addChar(' '); }
        }
        else if (base == "table" || base == "/table") { _flushLine(); }
        else if (base == "hr") {
            _flushLine();
            for (int i = 0; i < PE_LINE_WIDTH; i++) _addChar('-');
            _flushLine();
        }
        else if (base == "ul" || base == "ol") {
            _flushLine();
            if (_listDepth < PE_MAX_LIST_DEPTH) {
                _listIsOrdered[_listDepth] = (base == "ol");
                _listCounter[_listDepth]   = 0;
                _listDepth++;
            }
        }
        else if (base == "/ul" || base == "/ol") {
            _flushLine();
            if (_listDepth > 0) _listDepth--;
        }
        else if (base == "li") {
            _flushLine();
            int depth = max(0, _listDepth - 1);
            for (int i = 0; i < depth; i++) { _addChar(' '); _addChar(' '); }
            if (_listDepth > 0 && _listIsOrdered[depth]) {
                _listCounter[depth]++;
                String num = String(_listCounter[depth]) + ". ";
                for (int k = 0; k < (int)num.length(); k++) _addChar(num[k]);
            } else {
                _addChar('*'); _addChar(' ');
            }
        }
        else if (base == "blockquote") {
            _flushLine();
            _addChar(' '); _addChar(' '); _addChar('"'); _addChar(' ');
            _addMarkerPair('\x07', (char)(4 + 32));  // palette id 4 = dim, see paletteColor()
        }
        else if (base == "/blockquote") { _addChar('\x08'); _flushLine(); }
        else if (base == "pre" || base == "code") {
            _flushLine();
            _addMarkerPair('\x07', (char)(4 + 32));
        }
        else if (base == "/pre" || base == "/code") { _addChar('\x08'); _flushLine(); }
        else if (base == "h1" || base == "h2" || base == "h3") {
            _flushLine();
            _headingActive = true;
            _headingBig    = (base != "h3");
        }
        else if (base == "/h1" || base == "/h2" || base == "/h3") {
            _flushLine();
            _headingActive = false;
            _headingBig    = false;
        }
        else if (base == "form") {
            if (formCount < PE_MAX_FORMS) {
                String method = _getAttr(_tag, "method"); method.toLowerCase();
                forms[formCount].action = _getAttr(_tag, "action");
                forms[formCount].isGet  = (method != "post");
                _curForm = formCount;
                formCount++;
            } else {
                _curForm = -1;
            }
        }
        else if (base == "/form") { _curForm = -1; }
        else if (base == "input" || base == "button") {
            String type = _getAttr(_tag, "type"); type.toLowerCase();
            if (type == "" && base == "button") type = "submit";
            if (type == "") type = "text";
            bool isSubmit  = (type == "submit" || type == "button" || type == "image");
            bool isHidden  = (type == "hidden");
            bool isUnsupported = (type == "checkbox" || type == "radio" ||
                                  type == "file" || type == "reset");
            String name  = _getAttr(_tag, "name");
            String value = _getAttr(_tag, "value");

            if (isHidden) {
                // Not shown, not selectable - just rides along on submit.
                if (fieldCount < PE_MAX_FIELDS) {
                    fields[fieldCount++] = { name, value, false, _curForm };
                }
            } else if (!isUnsupported && fieldCount < PE_MAX_FIELDS &&
                       selectableCount < PE_MAX_SELECT) {
                int fid = fieldCount;
                fields[fieldCount] = { name, isSubmit ? "" : value, isSubmit, _curForm };
                _addMarkerPair('\x03', (char)(fid + 32));

                // <input> is void (see _isVoidElement) so it never went
                // through the generic CSS push above - apply any matching
                // style directly around just this bracket label instead,
                // since (unlike a real element) we already know exactly
                // where its "content" starts and ends: right here.
                CssStyle inputSt;
                bool inputBold = false;
                if (base == "input") {
                    inputSt = _resolveStyle("input", _tag);
                    if (inputSt.bold) { _addChar('\x05'); inputBold = true; }
                    if (inputSt.hasColor) _addMarkerPair('\x07', (char)(inputSt.colorId + 32));
                }

                String label = isSubmit ? (value.length() > 0 ? value : String("Submit"))
                                         : (value.length() > 0 ? value : String("..."));
                if (label.length() > 12) label = label.substring(0, 12);
                _addChar('[');
                for (int k = 0; k < (int)label.length(); k++) _addChar(label[k]);
                _addChar(']');

                if (base == "input") {
                    if (inputSt.hasColor) _addChar('\x08');
                    if (inputBold) _addChar('\x06');
                }

                _addChar('\x04');
                selectables[selectableCount++] = { EK_FIELD, fid };
                fieldCount++;
            }
        }
        else if (base == "img") {
            String src = _getAttr(_tag, "src");
            if (src.length() > 0 && imgCount < PE_MAX_IMAGES) imgURLs[imgCount++] = src;
        }
        else if (base == "meta" && !metaRefreshFound) {
            String httpEquiv = _getAttr(_tag, "http-equiv");
            httpEquiv.toLowerCase();
            if (httpEquiv == "refresh") {
                String content = _getAttr(_tag, "content");
                int semi = content.indexOf(';');
                String secsPart = (semi >= 0) ? content.substring(0, semi) : content;
                secsPart.trim();
                String target = "";
                if (semi >= 0) {
                    String rest = content.substring(semi + 1);
                    int uidx = rest.indexOf("url=");
                    if (uidx < 0) uidx = rest.indexOf("URL=");
                    if (uidx < 0) uidx = rest.indexOf("Url=");
                    if (uidx >= 0) {
                        target = rest.substring(uidx + 4);
                        target.trim();
                        if (target.startsWith("\"") || target.startsWith("'")) target = target.substring(1);
                        int qend = target.length() - 1;
                        if (qend >= 0 && (target[qend] == '"' || target[qend] == '\'')) target = target.substring(0, qend);
                    }
                }
                metaRefreshFound = true;
                metaRefreshSecs  = secsPart.toInt();
                metaRefreshURL   = target;
            }
        }

        if (isClose) _popStyleFrame(tagName);

        // Applied here, at the very end - NOT alongside the other pushes
        // near the top - because p/div/tr/li above already flush the
        // previous block's line as part of their own handling higher in
        // this function. Emitting the new block's color/bold marker before
        // that flush would land it on the tail of the OLD (about to be
        // flushed) line instead of the start of the fresh one the new
        // block's actual text is about to be written into - exactly the
        // kind of "marker on the wrong line" bug the _addMarkerPair
        // line-break guard exists to prevent elsewhere in this file.
        if (!isClose && isOptionalClose) _openBlockScopedStyle(tagName, _tag);
    }


    void _decodeEntity(const String& ent) {
        if (ent.startsWith("#")) {
            int code = ent.startsWith("#x")
                ? strtol(ent.substring(2).c_str(), NULL, 16)
                : ent.substring(1).toInt();
            if (code >= 1040 && code <= 1087)      { _addChar(0xD0); _addChar(0x90 + (code - 1040)); }
            else if (code >= 1088 && code <= 1103) { _addChar(0xD1); _addChar(0x80 + (code - 1088)); }
            else if (code == 1025) { _addChar(0xD0); _addChar(0x81); }
            else if (code == 1105) { _addChar(0xD1); _addChar(0x91); }
            else if (code > 31 && code < 127) _addChar((char)code);
        } else {
            if      (ent == "nbsp") _addChar(' ');
            else if (ent == "lt")   _addChar('<');
            else if (ent == "gt")   _addChar('>');
            else if (ent == "amp")  _addChar('&');
            else if (ent == "quot") _addChar('"');
            else if (ent == "apos") _addChar('\'');
            // Typographic entities mapped to plain-ASCII lookalikes - the
            // display font only carries ASCII + Cyrillic glyphs, so real
            // Unicode dashes/quotes would just render as boxes.
            else if (ent == "mdash" || ent == "ndash") { _addChar('-'); }
            else if (ent == "hellip") { _addChar('.'); _addChar('.'); _addChar('.'); }
            else if (ent == "laquo" || ent == "raquo" ||
                     ent == "ldquo" || ent == "rdquo") { _addChar('"'); }
            else if (ent == "lsquo" || ent == "rsquo") { _addChar('\''); }
            else if (ent == "times")  _addChar('x');
            else if (ent == "copy")   _addChar('c');
            else if (ent == "reg")    _addChar('r');
            else if (ent == "bull")   _addChar('*');
        }
    }

    void _addChar(char c) {
        if (lineCount >= PE_MAX_LINES) return;
        _line += c;
        if ((int)_line.length() > PE_LINE_WIDTH) {
            int sp = _line.lastIndexOf(' ');
            if (sp > 0) {
                String part = _line.substring(0, sp);
                String cont = _line.substring(sp + 1);
                if (onLineReady) onLineReady(part, lineCount, _currentLineFlags());
                lineCount++;
                _line = "";
                if (_activeLinkId >= 0 && lineCount < PE_MAX_LINES) {
                    _line += '\x01';
                    _line += (char)(_activeLinkId + 32);
                }
                // Carry bold across a mid-span wrap too, same reasoning as
                // the link marker above. Color isn't carried the same way -
                // it would need remembering which of the 5 palette ids was
                // active, for a case (a colored span wrapping mid-line)
                // rare enough on this narrow a display to not be worth it.
                if (_boldDepth > 0 && lineCount < PE_MAX_LINES) _line += '\x05';
                _line += cont;
            } else {
                _flushLine();
            }
        }
    }

    // Atomically inserts a 2-byte marker (color-escape + id), used for both
    // link-open ('\x01') and field ('\x03') markers. Going through _addChar
    // twice for this used to risk the id byte landing on a different
    // display line than its marker byte if a wrap fell between the two
    // calls, corrupting the highlight; this instead forces a line break
    // *before* the pair if it wouldn't otherwise fit, so the two bytes are
    // always written to the same _line together.
    void _addMarkerPair(char marker, char id) {
        if (lineCount >= PE_MAX_LINES) return;
        if ((int)_line.length() + 2 > PE_LINE_WIDTH) _flushLine();
        if (lineCount >= PE_MAX_LINES) return;
        _line += marker;
        _line += id;
    }

    void _flushLine() {
        if (_line.length() == 0 || lineCount >= PE_MAX_LINES) return;
        if (onLineReady) onLineReady(_line, lineCount, _currentLineFlags());
        lineCount++;
        _line = "";
        _lastSpace = true;
        // See _activeLinkId's comment: keep a link's highlight going across
        // block-level breaks (<br>/<p>/<div>/<tr>) that land inside it.
        if (_activeLinkId >= 0 && lineCount < PE_MAX_LINES) {
            _line += '\x01';
            _line += (char)(_activeLinkId + 32);
        }
        if (_boldDepth > 0 && lineCount < PE_MAX_LINES) _line += '\x05';
    }
};