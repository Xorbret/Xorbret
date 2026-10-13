#pragma once
#include <ctype.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <Preferences.h>
#include "paper_engine.h"
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/wifi_manager.h"
#include "../../config.h"

#define BR_MAX_TABS       3   // simultaneous tabs (see BrTab below)
#define BR_MAX_TAB_HIST   8   // back/forward entries kept per tab
#define BR_MAX_BOOKMARKS  8
#define BR_MAX_FIND      20   // find-in-page: max matching lines listed
// Minimum time between two fetches, regardless of target - a crude stand-in
// for "a human is clicking this, not a script". Free anti-abuse services
// (w3.org's html2txt among them) IP-ban for hours if they see requests
// arriving faster than this.
#define BR_MIN_FETCH_GAP_MS 1200
#define BR_VERSION        "2.0"
#define BR_AUTHOR         "Artem76228"

// Set by the browser before launch("Files") so a downloaded image opens
// straight in the File Manager's real image viewer instead of the plain
// file list - same pending-file handoff pattern as Music's
// g_musicPendingFile and 3D Editor's g_editor3dPendingFile. Declared here,
// not in filemanager.h, because browser.h is #included before
// filemanager.h in main.cpp - the reverse order would reference this
// before its declaration and fail to compile (see g_musicPendingFile /
// g_editor3dPendingFile, which filemanager.h currently references that
// way - a pre-existing bug, not repeated here).
static char g_imagePendingFile[128] = "";

// Search engine used when the URL bar gets a plain query instead of a URL.
// Big engines (Google, DuckDuckGo, Bing) increasingly block non-browser
// TLS clients at the connection level (TLS/JA3 fingerprinting), which no
// amount of HTTP header tweaking can fix from this hardware - if one
// engine starts failing, this is the one line to change to try another:
//   "https://www.mojeek.com/search?q="        (independent index, default)
//   "https://html.duckduckgo.com/html/?q="    (no-JS, but bot-checks hard)
//   "https://search.marginalia.nu/search?query="  (small indie index)
#define BR_SEARCH_URL  "https://www.mojeek.com/search?q="

//  PaperWeb Browser v2.0 - PaperOS web browser
//  by Artem76228 | github.com/Artem76228
//
//  Features:
//   - HTTP/HTTPS fetch via PaperEngine
//   - 3 tabs, each with its own back/forward history (up to 8 hops)
//   - Bookmarks (up to 8, in Preferences)
//   - Save page to /MitamaOS/saved_links/ on SD (.txt)
//   - Mojeek HTML search (input without http) - see BR_SEARCH_URL
//   - Separate W3 text mode via html2txt (W)
//   - Page title shown in header when the page has a <title>
//   - Real HTTP status/error shown on failed loads (404, 500, timeouts...)
//   - Page outline (O) - jump between h1/h2/h3 headings
//   - Find in page (F) - jump between lines matching a query
//   - Images on page (I) - list <img> URLs, download one to SD and hand
//     it off to the File Manager's real image viewer
//   - <meta http-equiv="refresh"> auto-follow, capped at 3 hops
//   - Link navigation with highlighting
//   - resolveURL for relative links
//   - Splash screen on launch
//   - Load progress bar
//   - In-app help screen (?) listing every shortcut
//
//  Keys (reading mode):
//   ; / .   = scroll up/down (1 line)
//   , / /   = previous/next link
//   ENT     = open selected link
//   U       = enter URL manually
//   DEL     = back        ]  = forward
//   B       = bookmark
//   K       = bookmark list
//   H       = history (current tab)
//   O       = page outline (jump to heading)
//   F       = find in page
//   I       = images on page
//   T       = tab switcher     1/2/3 = jump to tab
//   D       = save page as TXT to SD
//   ?       = help screen
//   `       = exit

class Browser : public App {
public:
    Browser(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "PaperWeb"; }
    const char* getIcon() const override { return "W"; }
    const char* getDesc() const override { return "Web browser"; }

    void onStart() override {
        _isLoading  = false;
        _isReading  = false;
        _scrollPos  = 0;
        _selLink    = 0;
        _lineCount  = 0;
        _loadPct    = 0;
        _curTab     = 0;
        for (int i = 0; i < BR_MAX_TABS; i++) _tabs[i] = BrTab();

        _instance = this;
        _engine.onLineReady = [](String& line, int lineNum, uint8_t flags) {
            if (_instance && lineNum < PE_MAX_LINES) {
                _instance->_dispLines[lineNum] = line;
                _instance->_lineFlags[lineNum] = flags;
                _instance->_lineCount = lineNum + 1;
            }
        };

        _loadBookmarks();
        _drawSplash();   // Branded splash screen
        delay(2000);

        _currentURL = "https://www.mojeek.com/";
        _draw();
    }
    void onStop() override {
        // The page text/links live in String arrays on this object, and
        // this object is a permanent global (never destroyed) - closing
        // the app alone doesn't free them. Without this, whatever was on
        // screen when you left stays allocated in RAM for the rest of the
        // session, which can starve a later app (myAI's ~83KB model cache
        // in particular) of the contiguous heap it needs.
        for (int i = 0; i <= PE_MAX_LINES; i++) _dispLines[i] = "";
        _lineCount = 0;
        _engine.pageTitle = "";
        for (int i = 0; i < PE_MAX_LINKS; i++) _engine.linkURLs[i] = "";
        for (int i = 0; i < PE_MAX_IMAGES; i++) _engine.imgURLs[i] = "";
        for (int i = 0; i < BR_MAX_TABS; i++) _tabs[i] = BrTab();
    }

    void update() override {
        M5Cardputer.update();
        auto keys = M5Cardputer.Keyboard.keysState();

        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            _draw(); _canvas.pushSprite(0, 0);
            if (OsUI::confirmExit(_canvas)) AppManager::get().goHome();
            else _draw();
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('?')) {
            OsUI::waitRelease();
            _showHelp();
            _draw();
            return;
        }

        if (_isReading) _handleReadingKeys(keys);
        else            _handleIdleKeys(keys);
    }

private:
    LGFX_Sprite& _canvas;
    PaperEngine  _engine;
    static Browser* _instance;

    String _currentURL = "";
    String _status     = "READY";
    bool   _isLoading  = false;
    bool   _isReading  = false;
    int    _scrollPos  = 0;
    int    _selLink    = 0;
    int    _maxScroll  = 0;
    int    _loadPct    = 0;
    int    _lastStatusCode = 0;  // 0 = no fetch yet, else PaperEngine::lastHttpCode
    bool   _isBlocked = false;   // page loaded fine (HTTP 200) but looks like a JS/bot-check challenge, not real content

    int    _outlineIdx[20];
    int    _outlineCount = 0;

    String  _dispLines[PE_MAX_LINES + 1];
    uint8_t _lineFlags[PE_MAX_LINES + 1] = {0};  // PE_LF_* bits, set alongside each line - see paper_engine.h
    int    _lineCount  = 0;

    // One tab = one independent back/forward history + a remembered scroll
    // position. Only the active tab's parsed page actually lives in memory
    // at any time (see _switchTab) - re-fetching a backgrounded tab on
    // switch-back is the deliberate tradeoff for staying inside this
    // hardware's RAM budget instead of keeping 3 full copies of a page.
    struct BrTab {
        String url       = "";
        String title     = "";
        String hist[BR_MAX_TAB_HIST];
        int    histCount  = 0;
        int    histPos    = -1;
        int    scrollPos  = 0;
    };
    BrTab _tabs[BR_MAX_TABS];
    int   _curTab = 0;

    String _bookmarks[BR_MAX_BOOKMARKS];
    int    _bmarkCount = 0;

    void _drawSplash() {
        _canvas.fillSprite(COL_BG);
        _canvas.setTextSize(2);
        _canvas.setTextColor(COL_ACCENT, COL_BG);
        _canvas.drawCenterString("PaperWeb", 120, 22);

        _canvas.setTextSize(1);
        _canvas.setTextColor(COL_DIM, COL_BG);
        _canvas.drawCenterString("v" BR_VERSION, 120, 52);

        _canvas.drawFastHLine(30, 62, 180, COL_SELECTED);

        _canvas.setTextColor(COL_TEXT, COL_BG);
        _canvas.drawCenterString("by " BR_AUTHOR, 120, 72);

        _canvas.setTextColor(COL_DIM, COL_BG);
        _canvas.drawCenterString("github.com/" BR_AUTHOR, 120, 86);

        // Status bar at the bottom
        _canvas.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, COL_FOOTER);
        _canvas.drawFastHLine(0, SCREEN_H - FOOTER_H, SCREEN_W, COL_ACCENT);
        _canvas.setTextColor(COL_DIM, COL_FOOTER);
        _canvas.setCursor(5, SCREEN_H - FOOTER_H + 3);
        _canvas.print("Loading " OS_NAME " Browser...");

        _canvas.pushSprite(0, 0);
    }

    void _handleReadingKeys(decltype(M5Cardputer.Keyboard.keysState()) keys) {
        bool redraw = false;

        // Scroll by 1 line (slower than 3)
        if (M5Cardputer.Keyboard.isKeyPressed(';')) {
            _scrollPos = max(0, _scrollPos - 1);
            delay(60); redraw = true;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('.')) {
            _scrollPos = min(_maxScroll, _scrollPos + 1);
            delay(60); redraw = true;
        }

        // Switch between links and form fields (doc order)
        if (M5Cardputer.Keyboard.isKeyPressed(',')) {
            if (_selLink > 0) { _selLink--; redraw = true; }
            delay(KEY_REPEAT_MS);
        }
        if (M5Cardputer.Keyboard.isKeyPressed('/')) {
            if (_selLink < _engine.selectableCount - 1) { _selLink++; redraw = true; }
            delay(KEY_REPEAT_MS);
        }

        // Open link / edit field / submit form, depending on what's selected
        if (keys.enter && _engine.selectableCount > 0 && _selLink < _engine.selectableCount) {
            OsUI::waitRelease();
            auto& sel = _engine.selectables[_selLink];
            if (sel.kind == PaperEngine::EK_LINK) {
                String url = _resolveURL(_currentURL, _engine.linkURLs[sel.idx]);
                _navigate(url);
            } else {
                _activateField(sel.idx);
            }
            return;
        }

        // U - manual URL entry
        if (M5Cardputer.Keyboard.isKeyPressed('u')) {
            OsUI::waitRelease();
            String raw = OsUI::inputText(_canvas, "URL:");
            if (raw != "") {
                String url = _normalizeQuery(raw, false);
                _navigate(url);
            } else redraw = true;
            return;
        }

        // W - W3 text mode
        if (M5Cardputer.Keyboard.isKeyPressed('w') || M5Cardputer.Keyboard.isKeyPressed('W')) {
            OsUI::waitRelease();
            String raw = OsUI::inputText(_canvas, "W3 text URL:");
            if (raw != "") {
                String url = _normalizeQuery(raw, true);
                _navigate(url);
            } else redraw = true;
            return;
        }

        // DEL / ] - back / forward
        if (keys.del) { OsUI::waitRelease(); _goBack(); return; }
        if (M5Cardputer.Keyboard.isKeyPressed(']')) { OsUI::waitRelease(); _goForward(); return; }

        // B - bookmark
        if (M5Cardputer.Keyboard.isKeyPressed('b')) {
            OsUI::waitRelease();
            _addBookmark(_currentURL);
            OsUI::showToast(_canvas, "Bookmarked!", COL_OK);
            _canvas.pushSprite(0, 0); delay(700); redraw = true;
        }

        // K - bookmark list
        if (M5Cardputer.Keyboard.isKeyPressed('k')) {
            OsUI::waitRelease();
            String sel = _bookmarkMenu();
            if (sel != "") { _navigate(sel); return; }
            redraw = true;
        }

        // H - history
        if (M5Cardputer.Keyboard.isKeyPressed('h')) {
            OsUI::waitRelease();
            String sel = _historyMenu();
            if (sel != "") { _navigate(sel); return; }
            redraw = true;
        }

        // O - page outline (jump between headings)
        if (M5Cardputer.Keyboard.isKeyPressed('o')) {
            OsUI::waitRelease();
            _outlineMenu();
            redraw = true;
        }

        // F - find in page
        if (M5Cardputer.Keyboard.isKeyPressed('f')) {
            OsUI::waitRelease();
            _findMenu();
            redraw = true;
        }

        // I - images on page
        if (M5Cardputer.Keyboard.isKeyPressed('i')) {
            OsUI::waitRelease();
            _imagesMenu();
            redraw = true;
        }

        // T - tab switcher      1/2/3 - jump straight to a tab
        if (M5Cardputer.Keyboard.isKeyPressed('t')) {
            OsUI::waitRelease();
            _tabMenu();
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('1')) { OsUI::waitRelease(); _switchTab(0); return; }
        if (M5Cardputer.Keyboard.isKeyPressed('2')) { OsUI::waitRelease(); _switchTab(1); return; }
        if (M5Cardputer.Keyboard.isKeyPressed('3')) { OsUI::waitRelease(); _switchTab(2); return; }

        // D - save page as TXT to SD
        if (M5Cardputer.Keyboard.isKeyPressed('d')) {
            OsUI::waitRelease();
            _savePageToTXT();
            redraw = true;
        }

        if (redraw) _draw();
    }

    void _handleIdleKeys(decltype(M5Cardputer.Keyboard.keysState()) keys) {
        if (keys.enter) {
            OsUI::waitRelease();
            String raw = OsUI::inputText(_canvas, "Search or URL:");
            if (raw != "") {
                String url = _normalizeQuery(raw, false);
                _navigate(url);
            } else _draw();
            return;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('w') || M5Cardputer.Keyboard.isKeyPressed('W')) {
            OsUI::waitRelease();
            String raw = OsUI::inputText(_canvas, "W3 text URL:");
            if (raw != "") {
                String url = _normalizeQuery(raw, true);
                _navigate(url);
            } else _draw();
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('k')) {
            OsUI::waitRelease();
            String sel = _bookmarkMenu();
            if (sel != "") { _navigate(sel); return; }
            _draw();
        }
        if (M5Cardputer.Keyboard.isKeyPressed('h')) {
            OsUI::waitRelease();
            String sel = _historyMenu();
            if (sel != "") { _navigate(sel); return; }
            _draw();
        }
        if (M5Cardputer.Keyboard.isKeyPressed('t')) {
            OsUI::waitRelease();
            _tabMenu();
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('1')) { OsUI::waitRelease(); _switchTab(0); return; }
        if (M5Cardputer.Keyboard.isKeyPressed('2')) { OsUI::waitRelease(); _switchTab(1); return; }
        if (M5Cardputer.Keyboard.isKeyPressed('3')) { OsUI::waitRelease(); _switchTab(2); return; }
    }

    void _navigate(const String& url, bool addHist = true, int metaHops = 3) {
        if (!WiFiManager::get().isConnected()) {
            _status = "No WiFi!"; _isReading = false; _draw(); return;
        }

        BrTab& tab = _tabs[_curTab];
        if (addHist) {
            if (tab.histCount < BR_MAX_TAB_HIST) {
                tab.hist[tab.histCount++] = url;
            } else {
                for (int i = 1; i < BR_MAX_TAB_HIST; i++) tab.hist[i-1] = tab.hist[i];
                tab.hist[BR_MAX_TAB_HIST - 1] = url;
            }
            tab.histPos = tab.histCount - 1;
        }

        String referer = _currentURL;   // real browsers send the page you're leaving
        _currentURL = url;
        tab.url     = url;
        _status     = "Loading...";
        _isLoading  = true;
        _isReading  = false;
        _isBlocked  = false;
        _scrollPos  = 0;
        _selLink    = 0;
        _loadPct    = 0;

        for (int i = 0; i <= PE_MAX_LINES; i++) _dispLines[i] = "";
        _lineCount = 0;

        _draw();

        // Simple client-side throttle: don't let ENT-mashing (or a fast
        // back/forward loop) fire requests faster than a real person could
        // click. Free shared services like w3.org's html2txt track exactly
        // this pattern and IP-ban for several hours if triggered.
        static uint32_t s_lastFetchMs = 0;
        uint32_t nowMs = millis();
        uint32_t sinceLastMs = nowMs - s_lastFetchMs;
        if (s_lastFetchMs != 0 && sinceLastMs < BR_MIN_FETCH_GAP_MS) {
            delay(BR_MIN_FETCH_GAP_MS - sinceLastMs);
        }
        s_lastFetchMs = millis();

        _engine.reset();

        bool ok = _engine.fetch(url, referer, 30000, [](int dl, int total) {
            if (_instance) {
                _instance->_loadPct = (total > 0) ? min(99, dl * 100 / total) : 50;
                if (dl % 4096 < 512) _instance->_draw();
            }
        });

        _isLoading = false;
        _loadPct   = 100;
        _lastStatusCode = _engine.lastHttpCode;

        if (!ok || _lineCount == 0) {
            _isReading = false;
            int code = _lastStatusCode;
            if (code != 200) {
                _status = "ERR " + String(code);
                _dispLines[0] = "Error " + String(code) + ": " + PaperEngine::httpCodeText(code);
            } else {
                _status = "Empty";
                _dispLines[0] = "Page returned no text:";
            }
            _dispLines[1] = url.length() > 36 ? url.substring(0, 35) + "~" : url;
            _lineCount = 2;
            _maxScroll = max(0, _lineCount - 7);
            _draw(); return;
        }

        _status    = "OK " + String(_engine.bytesDownloaded / 1024) + "KB";
        _isReading = true;
        _maxScroll = max(0, _lineCount - 7);
        tab.title  = (_engine.pageTitle.length() > 0) ? _engine.pageTitle : url;
        _buildOutline();

        if (_looksLikeChallenge()) {
            _isBlocked = true;
            _isReading = false;
            _status    = "BLOCKED";
            _dispLines[0] = "This looks like a bot-check / JS challenge page,";
            _dispLines[1] = "not real content - can't pass it without a full";
            _dispLines[2] = "browser with JavaScript. Try a direct URL instead.";
            _lineCount = 3;
            _maxScroll = max(0, _lineCount - 7);
            _draw();
            return;
        }

        // Auto-follow <meta http-equiv="refresh"> AND the plain-text
        // JS-redirect patterns the engine scans for (see _scanJsRedirect
        // in paper_engine.h - no JS is actually executed, just pattern
        // matching), same as a real browser - capped at a few hops
        // (metaHops) so a page that refreshes to itself, or two pages that
        // refresh to each other, can't hang the browser in a silent loop
        // behind the user's back.
        if (_engine.metaRefreshFound && _engine.metaRefreshURL.length() > 0 && metaHops > 0) {
            String target = _resolveURL(url, _engine.metaRefreshURL);
            if (target.length() > 0 && target != url) {
                const char* msg = _engine.metaRefreshFromJS
                    ? "Redirect script detected..."   // honest: this was a text-pattern guess, not real meta-refresh
                    : "Page redirects...";
                OsUI::showToast(_canvas, msg, COL_WARN);
                _canvas.pushSprite(0, 0);
                delay(constrain(_engine.metaRefreshSecs, 0, 5) * 1000);
                _navigate(target, true, metaHops - 1);
                return;
            }
        }

        _draw();
    }

    // Cloudflare-style "verify you're human" pages often reply with a
    // normal HTTP 200, so the status-code check alone can't catch them -
    // this scans the rendered text for the phrases such pages use.
    bool _looksLikeChallenge() {
        static const char* sig[] = {
            "verification required", "checking your browser", "just a moment",
            "attention required", "please enable javascript", "access denied",
            "are you human", "unusual traffic", "complete the challenge",
            "verify you are human", "waiting for verification"
        };
        int checkLines = min(_lineCount, 12);  // these banners always sit near the top
        for (int i = 0; i < checkLines; i++) {
            String lo = _dispLines[i];
            lo.toLowerCase();
            for (const char* s : sig) {
                if (lo.indexOf(s) >= 0) return true;
            }
        }
        return false;
    }

    // Scans already-rendered lines for headings (tagged with the engine's
    // real PE_LF_HEADING flag for h1/h2/h3, see below) so the reader can
    // jump between sections instead of scrolling line by line - the
    // closest realistic stand-in for a full DOM/site tree on this hardware.
    void _buildOutline() {
        _outlineCount = 0;
        for (int i = 0; i < _lineCount && _outlineCount < 20; i++) {
            // Headings are now identified by the engine's real per-line
            // PE_LF_HEADING flag (see paper_engine.h) instead of a "[H] "
            // text prefix - real bold/big rendering already makes headings
            // visually obvious, so the old text marker would've just been
            // clutter on screen now.
            if (_lineFlags[i] & PE_LF_HEADING) _outlineIdx[_outlineCount++] = i;
        }
    }

    void _outlineMenu() {
        _lineJumpMenu("Page Outline", _outlineIdx, _outlineCount, 0);
    }

    // Prompts for a query, then lists every currently-displayed line that
    // contains it (case-insensitive) so the user can jump straight to a
    // match - the closest realistic stand-in for Ctrl+F on this hardware
    // (only what's already been fetched/rendered is searchable, not the
    // live network - there's no room to keep a raw-HTML copy around too).
    void _findMenu() {
        String q = OsUI::inputText(_canvas, "Find:");
        if (q.length() == 0) { return; }
        q.toLowerCase();
        static int s_matches[BR_MAX_FIND];
        int cnt = 0;
        for (int i = 0; i < _lineCount && cnt < BR_MAX_FIND; i++) {
            String lo = _dispLines[i];
            lo.toLowerCase();
            if (lo.indexOf(q) >= 0) s_matches[cnt++] = i;
        }
        char hdr[24];
        snprintf(hdr, sizeof(hdr), "%d match%s found", cnt, cnt == 1 ? "" : "es");
        _lineJumpMenu(hdr, s_matches, cnt, 0);
    }

    // Shared by the page outline and find-in-page: a scrollable pick-list
    // of already-rendered lines, ENT jumps _scrollPos to the chosen one.
    // stripLen drops a leading marker (e.g. "[H] ") from what's shown.
    void _lineJumpMenu(const char* header, int* idxArr, int count, int stripLen) {
        if (count == 0) {
            OsUI::showToast(_canvas, "Nothing found", COL_DIM);
            _canvas.pushSprite(0, 0); delay(800); return;
        }
        int sel = 0;
        while (true) {
            M5Cardputer.update();
            _canvas.fillSprite(COL_BG);
            OsUI::drawHeader(_canvas, header, COL_HEADER);
            _canvas.setTextSize(1);
            int vis = 7, scroll = max(0, sel - vis + 1);
            for (int i = scroll; i < count && i < scroll + vis; i++) {
                bool s = (i == sel);
                uint16_t bg = s ? COL_SELECTED : COL_BG;
                _canvas.fillRect(0, CONTENT_Y + (i - scroll) * 13, SCREEN_W, 12, bg);
                _canvas.setTextColor(s ? (uint16_t)WHITE : (uint16_t)COL_TEXT, bg);
                _canvas.setCursor(4, CONTENT_Y + (i - scroll) * 13 + 2);
                String h = _dispLines[idxArr[i]];
                if (stripLen > 0 && (int)h.length() >= stripLen) h = h.substring(stripLen);
                h = _stripMarkers(h);   // see its own comment - h/find results now almost
                                        // always carry a bold/color/link marker byte (every
                                        // heading is bold by default), which would otherwise
                                        // print as a garbage glyph in this list
                if (h.length() > 36) h = h.substring(0, 35) + "~";
                _canvas.print(h);
            }
            OsUI::drawFooter(_canvas, ";.=nav  ENT=jump  `=back");
            _canvas.pushSprite(0, 0);
            if (M5Cardputer.Keyboard.isKeyPressed(';')) { if (sel > 0) sel--; delay(KEY_REPEAT_MS); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { if (sel < count - 1) sel++; delay(KEY_REPEAT_MS); }
            auto st = M5Cardputer.Keyboard.keysState();
            if (st.enter) {
                OsUI::waitRelease();
                _scrollPos = min(idxArr[sel], _maxScroll);
                return;
            }
            if (M5Cardputer.Keyboard.isKeyPressed('`')) { OsUI::waitRelease(); return; }
            delay(5);
        }
    }

    // ── Tabs ────────────────────────────────────────────────────────────

    void _goBack() {
        BrTab& tab = _tabs[_curTab];
        if (tab.histPos > 0) {
            tab.histPos--;
            _navigate(tab.hist[tab.histPos], false);
        }
    }
    void _goForward() {
        BrTab& tab = _tabs[_curTab];
        if (tab.histPos >= 0 && tab.histPos < tab.histCount - 1) {
            tab.histPos++;
            _navigate(tab.hist[tab.histPos], false);
        }
    }

    // Switches the active tab. Only the current tab's page actually stays
    // in RAM (see BrTab's comment) - switching re-fetches the target tab's
    // last URL, the same tradeoff a memory-constrained mobile browser makes
    // when it discards a backgrounded tab and reloads it on return.
    void _switchTab(int idx) {
        if (idx < 0 || idx >= BR_MAX_TABS) return;
        _tabs[_curTab].scrollPos = _scrollPos;
        if (idx == _curTab) return;
        _curTab = idx;
        BrTab& tab = _tabs[_curTab];
        if (tab.url.length() > 0) {
            _navigate(tab.url, false);
            _scrollPos = min(tab.scrollPos, _maxScroll);
        } else {
            _currentURL = "";
            _isReading = _isLoading = _isBlocked = false;
            _lineCount = 0; _scrollPos = 0; _lastStatusCode = 0;
            for (int i = 0; i <= PE_MAX_LINES; i++) _dispLines[i] = "";
        }
        _draw();
    }

    void _tabMenu() {
        int sel = _curTab;
        while (true) {
            M5Cardputer.update();
            _canvas.fillSprite(COL_BG);
            OsUI::drawHeader(_canvas, "Tabs", COL_HEADER);
            _canvas.setTextSize(1);
            for (int i = 0; i < BR_MAX_TABS; i++) {
                bool s = (i == sel);
                uint16_t bg = s ? COL_SELECTED : COL_BG;
                _canvas.fillRect(0, CONTENT_Y + i * 16, SCREEN_W, 15, bg);
                _canvas.setTextColor(s ? (uint16_t)WHITE : (uint16_t)COL_TEXT, bg);
                _canvas.setCursor(4, CONTENT_Y + i * 16 + 2);
                String label = String(i + 1) + (i == _curTab ? "* : " : "  : ");
                String u = _tabs[i].title.length() > 0 ? _tabs[i].title : _tabs[i].url;
                if (u.length() == 0) u = "(empty)";
                if (u.length() > 30) u = u.substring(0, 29) + "~";
                _canvas.print(label + u);
            }
            OsUI::drawFooter(_canvas, ";.=nav ENT=switch D=close `=back");
            _canvas.pushSprite(0, 0);
            if (M5Cardputer.Keyboard.isKeyPressed(';')) { if (sel > 0) sel--; delay(KEY_REPEAT_MS); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { if (sel < BR_MAX_TABS - 1) sel++; delay(KEY_REPEAT_MS); }
            auto st = M5Cardputer.Keyboard.keysState();
            if (st.enter) { OsUI::waitRelease(); _switchTab(sel); return; }
            if (M5Cardputer.Keyboard.isKeyPressed('d')) {
                OsUI::waitRelease();
                _tabs[sel] = BrTab();
                if (sel == _curTab) {
                    _currentURL = ""; _isReading = _isLoading = _isBlocked = false;
                    _lineCount = 0; _scrollPos = 0; _lastStatusCode = 0;
                    for (int i = 0; i <= PE_MAX_LINES; i++) _dispLines[i] = "";
                }
            }
            if (M5Cardputer.Keyboard.isKeyPressed('`')) { OsUI::waitRelease(); return; }
            delay(5);
        }
    }

    // ── Images ──────────────────────────────────────────────────────────

    void _imagesMenu() {
        if (_engine.imgCount == 0) {
            OsUI::showToast(_canvas, "No images on page", COL_DIM);
            _canvas.pushSprite(0, 0); delay(800); return;
        }
        int sel = 0;
        while (true) {
            M5Cardputer.update();
            _canvas.fillSprite(COL_BG);
            OsUI::drawHeader(_canvas, "Images", COL_HEADER);
            _canvas.setTextSize(1);
            int vis = 7, scroll = max(0, sel - vis + 1);
            for (int i = scroll; i < _engine.imgCount && i < scroll + vis; i++) {
                bool s = (i == sel);
                uint16_t bg = s ? COL_SELECTED : COL_BG;
                _canvas.fillRect(0, CONTENT_Y + (i - scroll) * 13, SCREEN_W, 12, bg);
                _canvas.setTextColor(s ? (uint16_t)WHITE : (uint16_t)COL_TEXT, bg);
                _canvas.setCursor(4, CONTENT_Y + (i - scroll) * 13 + 2);
                String u = _engine.imgURLs[i];
                if (u.length() > 36) u = u.substring(0, 35) + "~";
                _canvas.print(u);
            }
            OsUI::drawFooter(_canvas, ";.=nav  ENT=save+view  `=back");
            _canvas.pushSprite(0, 0);
            if (M5Cardputer.Keyboard.isKeyPressed(';')) { if (sel > 0) sel--; delay(KEY_REPEAT_MS); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { if (sel < _engine.imgCount - 1) sel++; delay(KEY_REPEAT_MS); }
            auto st = M5Cardputer.Keyboard.keysState();
            if (st.enter) { OsUI::waitRelease(); _downloadAndViewImage(_engine.imgURLs[sel]); return; }
            if (M5Cardputer.Keyboard.isKeyPressed('`')) { OsUI::waitRelease(); return; }
            delay(5);
        }
    }

    // Downloads a page's <img src> to SD and hands it off to the File
    // Manager's real image viewer (BMP/PNG/JPG decoders already linked
    // into the firmware for icons/fonts) - this engine has no inline
    // image renderer of its own, so this is the realistic path to
    // actually seeing a page's pictures rather than just their URLs.
    void _downloadAndViewImage(const String& rawSrc) {
        if (SD.cardType() == CARD_NONE) {
            OsUI::showToast(_canvas, "No SD card!", COL_ERR);
            _canvas.pushSprite(0, 0); delay(900); return;
        }
        String url = _resolveURL(_currentURL, rawSrc);
        if (url.length() == 0) return;

        // Pick a plausible extension so the File Manager's own classifier
        // (bmp/png/jpg -> image viewer) reaches for the right decoder;
        // default to .jpg for query-string image endpoints with no
        // recognizable suffix (the common case on real sites).
        String lo = url; lo.toLowerCase();
        const char* exts[] = { ".png", ".jpg", ".jpeg", ".bmp", ".qoi" };
        String ext = ".jpg";
        for (const char* e : exts) { if (lo.indexOf(e) >= 0) { ext = e; break; } }

        if (!SD.exists("/MitamaOS")) SD.mkdir("/MitamaOS");
        if (!SD.exists("/MitamaOS/browser_img")) SD.mkdir("/MitamaOS/browser_img");
        String path = "/MitamaOS/browser_img/img_" + String(millis()) + ext;

        OsUI::showToast(_canvas, "Downloading image...", COL_ACCENT);
        _canvas.pushSprite(0, 0);

        bool ok = _engine.fetchToFile(url, _currentURL, path, 20000);
        if (!ok) {
            OsUI::showToast(_canvas, "Download failed", COL_ERR);
            _canvas.pushSprite(0, 0); delay(900);
            return;
        }

        strncpy(g_imagePendingFile, path.c_str(), sizeof(g_imagePendingFile) - 1);
        g_imagePendingFile[sizeof(g_imagePendingFile) - 1] = '\0';
        OsUI::showToast(_canvas, "Saved - opening viewer...", COL_OK);
        _canvas.pushSprite(0, 0); delay(500);
        AppManager::get().launch("Files");
    }

    // ── Help ────────────────────────────────────────────────────────────

    void _showHelp() {
        _canvas.fillSprite(COL_BG);
        OsUI::drawHeader(_canvas, "PaperWeb Help", COL_HEADER);
        _canvas.setTextSize(1);
        _canvas.setTextColor(COL_TEXT, COL_BG);
        static const char* lines[] = {
            "ENT  open URL/search, or link",
            "; .  scroll        , /  prev/next link",
            "DEL  back          ]    forward",
            "U    URL bar       W    W3 text mode",
            "B    bookmark      K    bookmark list",
            "H    history       O    page outline",
            "F    find in page  I    images on page",
            "T    tab switcher  1/2/3  jump to tab",
            "D    save page as TXT to SD",
            "`    exit / back"
        };
        for (int i = 0; i < 10; i++) {
            _canvas.setCursor(3, CONTENT_Y + 1 + i * 10);
            _canvas.print(lines[i]);
        }
        OsUI::drawFooter(_canvas, "ENT or ` = close");
        _canvas.pushSprite(0, 0);
        while (true) {
            M5Cardputer.update();
            auto st = M5Cardputer.Keyboard.keysState();
            if (st.enter || M5Cardputer.Keyboard.isKeyPressed('`')) { OsUI::waitRelease(); return; }
            delay(10);
        }
    }

    // Strips the escape marker bytes (see the legend in paper_engine.h's
    // _handleTag) from a rendered line, leaving plain readable text. The
    // save-to-TXT feature always wrote _dispLines[] out raw, which meant
    // saved pages already carried invisible link/field marker bytes -
    // adding the bold/color markers made that noticeably worse (they fire
    // on far more real pages than links/forms alone), so this is worth
    // fixing now: a .txt someone saves to read later, or opens on a PC,
    // should be plain text, not text sprinkled with control bytes.
    static String _stripMarkers(const String& line) {
        String out;
        out.reserve(line.length());
        for (int i = 0; i < (int)line.length(); i++) {
            char c = line[i];
            if (c == '\x01' || c == '\x03' || c == '\x07') { i++; continue; }  // marker + id byte
            if (c == '\x02' || c == '\x04' || c == '\x05' || c == '\x06' || c == '\x08') continue;
            out += c;
        }
        return out;
    }

    void _savePageToTXT() {
        if (SD.cardType() == CARD_NONE) {
            OsUI::showToast(_canvas, "No SD card!", COL_ERR);
            _canvas.pushSprite(0, 0); delay(900); return;
        }
        if (!SD.exists("/MitamaOS")) SD.mkdir("/MitamaOS");
        if (!SD.exists("/MitamaOS/saved_links")) SD.mkdir("/MitamaOS/saved_links");

        String fname = "/MitamaOS/saved_links/page_" + String(millis()) + ".txt";
        File f = SD.open(fname, FILE_WRITE);
        if (!f) {
            OsUI::showToast(_canvas, "Write failed!", COL_ERR);
            _canvas.pushSprite(0, 0); delay(900); return;
        }
        f.println("URL: " + _currentURL);
        f.println("====================");
        for (int i = 0; i < _lineCount; i++) f.println(_stripMarkers(_dispLines[i]));
        f.close();
        OsUI::showToast(_canvas, "Saved to SD!", COL_OK);
        _canvas.pushSprite(0, 0); delay(800);
    }

    String _normalizeQuery(const String& raw, bool w3TextMode) {
        String input = raw;
        input.trim();
        if (input.length() == 0) return "";

        if (w3TextMode) {
            String page = input;
            if (!page.startsWith("http://") && !page.startsWith("https://")) page = "https://" + page;
            // The target URL must be percent-encoded as a single query value -
            // otherwise any "&", "?", "#" inside it (very common in real URLs)
            // gets parsed as extra params by w3.org's own server instead of
            // being part of the ?url= value, silently breaking the request.
            return "https://www.w3.org/services/html2txt?url=" + _urlEncodeFull(page);
        }

        if (input.indexOf("://") >= 0) return input;
        if (input.indexOf(' ') >= 0 || input.indexOf('.') == -1) {
            return String(BR_SEARCH_URL) + _urlEncode(input);
        }
        if (!input.startsWith("http://") && !input.startsWith("https://")) {
            return "https://" + input;
        }
        return input;
    }

    // Percent-encodes an entire URL so it can be embedded as one query-string
    // value (e.g. ?url=<this>) without its own "&"/"?"/"#" being parsed as
    // separate params by the receiving server. Unlike _urlEncode() below,
    // this does NOT turn spaces into '+' (not meaningful for embedded URLs)
    // and leaves the encoding otherwise identical.
    static String _urlEncodeFull(const String& s) {
        String out;
        char buf[4];
        for (int i = 0; i < (int)s.length(); i++) {
            uint8_t c = (uint8_t)s[i];
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
            else { sprintf(buf, "%%%02X", c); out += buf; }
        }
        return out;
    }

    // Percent-encodes a search query, including UTF-8 bytes (e.g. Cyrillic
    // input) - sending them raw in a GET query string works on some servers
    // but not reliably on all, and this is what a real browser does.
    static String _urlEncode(const String& s) {
        String out;
        char buf[4];
        for (int i = 0; i < (int)s.length(); i++) {
            uint8_t c = (uint8_t)s[i];
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
            else if (c == ' ') out += '+';
            else { sprintf(buf, "%%%02X", c); out += buf; }
        }
        return out;
    }

    String _resolveURL(const String& base, const String& rel) {
        if (rel.startsWith("http"))  return rel;
        if (rel.startsWith("//"))    return "https:" + rel;
        if (rel.startsWith("javascript:")) return "";
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

    // ENT on a form field: text-like inputs open the on-screen keyboard to
    // edit their value; submit/button inputs build the form's URL and
    // navigate there.
    void _activateField(int fieldIdx) {
        if (fieldIdx < 0 || fieldIdx >= _engine.fieldCount) return;
        auto& f = _engine.fields[fieldIdx];
        if (f.isSubmit) {
            if (f.formId >= 0 && !_engine.forms[f.formId].isGet) {
                OsUI::showToast(_canvas, "POST form - sending as GET", COL_WARN);
                _canvas.pushSprite(0, 0); delay(900);
            }
            String url = _buildSubmitURL(f.formId);
            if (url.length() == 0) {
                OsUI::showToast(_canvas, "No form action", COL_ERR);
                _canvas.pushSprite(0, 0); delay(800); _draw();
                return;
            }
            _navigate(url);
        } else {
            String v = OsUI::inputText(_canvas, "Value:");
            if (v != "") f.value = v;   // empty return = cancelled, keep old value
            _draw();
        }
    }

    // Collects every field belonging to formId (text fields, hidden fields,
    // and whatever the current values are) into a query string appended to
    // the form's action URL. Only GET submission is actually possible here -
    // PaperEngine::fetch() only speaks GET - so POST forms get a heads-up
    // toast (above) rather than silently mis-submitting.
    String _buildSubmitURL(int formId) {
        String base = _currentURL;
        if (formId >= 0 && formId < _engine.formCount) {
            const String& action = _engine.forms[formId].action;
            base = (action.length() > 0) ? _resolveURL(_currentURL, action) : _currentURL;
        }
        String qs = "";
        for (int i = 0; i < _engine.fieldCount; i++) {
            auto& f = _engine.fields[i];
            if (f.formId != formId || f.isSubmit || f.name.length() == 0) continue;
            if (qs.length() > 0) qs += "&";
            qs += _urlEncode(f.name) + "=" + _urlEncode(f.value);
        }
        if (qs.length() == 0) return base;
        return base + (base.indexOf('?') >= 0 ? "&" : "?") + qs;
    }

    String _bookmarkMenu() {
        if (_bmarkCount == 0) {
            OsUI::showToast(_canvas, "No bookmarks", COL_DIM);
            _canvas.pushSprite(0, 0); delay(800); return "";
        }
        int sel = 0;
        while (true) {
            M5Cardputer.update();
            _canvas.fillSprite(COL_BG);
            OsUI::drawHeader(_canvas, "Bookmarks", COL_HEADER);
            _canvas.setTextSize(1);
            for (int i = 0; i < _bmarkCount; i++) {
                bool s = (i == sel);
                uint16_t bg = s ? COL_SELECTED : COL_BG;
                _canvas.fillRect(0, CONTENT_Y + i * 14, SCREEN_W, 13, bg);
                _canvas.setTextColor(s ? (uint16_t)WHITE : (uint16_t)COL_TEXT, bg);
                _canvas.setCursor(4, CONTENT_Y + i * 14 + 2);
                String u = _bookmarks[i];
                if (u.length() > 36) u = u.substring(0, 35) + "~";
                _canvas.print(u);
            }
            OsUI::drawFooter(_canvas, ";.=nav  ENT=open  D=del  `=back");
            _canvas.pushSprite(0, 0);

            if (M5Cardputer.Keyboard.isKeyPressed(';')) { if (sel > 0) sel--; delay(KEY_REPEAT_MS); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { if (sel < _bmarkCount - 1) sel++; delay(KEY_REPEAT_MS); }
            auto st = M5Cardputer.Keyboard.keysState();
            if (st.enter)  { OsUI::waitRelease(); return _bookmarks[sel]; }
            if (M5Cardputer.Keyboard.isKeyPressed('d')) {
                OsUI::waitRelease(); _delBookmark(sel);
                if (sel >= _bmarkCount && sel > 0) sel--;
            }
            if (M5Cardputer.Keyboard.isKeyPressed('`')) { OsUI::waitRelease(); return ""; }
            delay(5);
        }
    }

    // History is per-tab (see BrTab) - this shows the current tab's own
    // back/forward list, not a single browser-wide list.
    String _historyMenu() {
        BrTab& tab = _tabs[_curTab];
        if (tab.histCount == 0) {
            OsUI::showToast(_canvas, "No history", COL_DIM);
            _canvas.pushSprite(0, 0); delay(800); return "";
        }
        int sel = tab.histCount - 1;
        while (true) {
            M5Cardputer.update();
            _canvas.fillSprite(COL_BG);
            OsUI::drawHeader(_canvas, "History", COL_HEADER);
            _canvas.setTextSize(1);
            int vis = 7, scroll = max(0, sel - vis + 1);
            for (int i = scroll; i < tab.histCount && i < scroll + vis; i++) {
                bool s = (i == sel);
                uint16_t bg = s ? COL_SELECTED : COL_BG;
                _canvas.fillRect(0, CONTENT_Y + (i - scroll) * 13, SCREEN_W, 12, bg);
                _canvas.setTextColor(s ? (uint16_t)WHITE : (uint16_t)COL_DIM, bg);
                _canvas.setCursor(4, CONTENT_Y + (i - scroll) * 13 + 2);
                String u = tab.hist[i];
                if (u.length() > 36) u = u.substring(0, 35) + "~";
                _canvas.print(u);
            }
            OsUI::drawFooter(_canvas, ";.=nav  ENT=open  `=back");
            _canvas.pushSprite(0, 0);
            if (M5Cardputer.Keyboard.isKeyPressed(';')) { if (sel > 0) sel--; delay(KEY_REPEAT_MS); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { if (sel < tab.histCount - 1) sel++; delay(KEY_REPEAT_MS); }
            auto st = M5Cardputer.Keyboard.keysState();
            if (st.enter) { OsUI::waitRelease(); return tab.hist[sel]; }
            if (M5Cardputer.Keyboard.isKeyPressed('`')) { OsUI::waitRelease(); return ""; }
            delay(5);
        }
    }

    void _loadBookmarks() {
        _bmarkCount = 0;
        Preferences p; p.begin("bmarks", true);
        int cnt = p.getInt("cnt", 0);
        for (int i = 0; i < cnt && i < BR_MAX_BOOKMARKS; i++)
            _bookmarks[_bmarkCount++] = p.getString(("u" + String(i)).c_str(), "");
        p.end();
    }
    void _addBookmark(const String& url) {
        if (_bmarkCount < BR_MAX_BOOKMARKS) { _bookmarks[_bmarkCount++] = url; }
        else {
            for (int i = 1; i < BR_MAX_BOOKMARKS; i++) _bookmarks[i-1] = _bookmarks[i];
            _bookmarks[BR_MAX_BOOKMARKS-1] = url;
        }
        _saveBookmarks();
    }
    void _delBookmark(int idx) {
        for (int i = idx; i < _bmarkCount - 1; i++) _bookmarks[i] = _bookmarks[i+1];
        if (_bmarkCount > 0) _bmarkCount--;
        _saveBookmarks();
    }
    void _saveBookmarks() {
        Preferences p; p.begin("bmarks", false);
        p.putInt("cnt", _bmarkCount);
        for (int i = 0; i < _bmarkCount; i++)
            p.putString(("u" + String(i)).c_str(), _bookmarks[i]);
        p.end();
    }

    void _draw() {
        _canvas.fillSprite(COL_BG);

        // Header - color-coded by what actually happened, not just OK/not-OK:
        // 2xx green-ish header, 3xx amber (redirect not followed), 4xx/5xx/
        // connection errors red, bot-check challenge its own distinct state.
        String hdr;
        bool isRedirect = (_lastStatusCode >= 300 && _lastStatusCode < 400);
        bool isError = !_isLoading && !_isReading && !_isBlocked && !isRedirect &&
                       (_lastStatusCode != 0 && _lastStatusCode != 200);

        if (_isLoading) hdr = "PaperWeb " + String(_loadPct) + "% ";
        else if (_isReading) hdr = "PaperWeb OK ";
        else if (_isBlocked) hdr = "PaperWeb BLOCKED ";
        else if (isRedirect) hdr = "PaperWeb WARN " + String(_lastStatusCode) + " ";
        else if (isError) hdr = "PaperWeb ERR " + String(_lastStatusCode) + " ";
        else hdr = "PaperWeb v" BR_VERSION " ";

        // Prefer the page's own <title> (real browsers do this too); fall
        // back to the bare hostname if the page had none.
        String host;
        if (_isReading && _engine.pageTitle.length() > 0) {
            host = _engine.pageTitle;
        } else {
            host = _currentURL;
            int p3 = host.indexOf("://");
            if (p3 >= 0) host = host.substring(p3 + 3);
            int sl = host.indexOf('/'); if (sl >= 0) host = host.substring(0, sl);
        }
        if (host.length() > 16) host = host.substring(0, 15) + "~";
        hdr += host;

        uint16_t hcol = _isLoading ? COL_HEADER_LOAD :
                        _isBlocked ? COL_HEADER_BAD :
                        isRedirect ? COL_HEADER_WARN :
                        isError    ? COL_HEADER_BAD :
                        (WiFiManager::get().isConnected() ? COL_HEADER : COL_HEADER_BAD);
        OsUI::drawHeader(_canvas, hdr.c_str(), hcol);

        // Progress bar
        if (_isLoading && _loadPct > 0 && _loadPct < 100) {
            int pw = map(_loadPct, 0, 100, 0, SCREEN_W);
            _canvas.fillRect(0, HEADER_H, pw, 2, COL_ACCENT);
        }

        _canvas.setTextSize(1);

        if (_isLoading) {
            _canvas.setTextColor(COL_ACCENT, COL_BG);
            _canvas.drawCenterString("Fetching...", 120, 55);
            String us = _currentURL; if (us.length() > 36) us = us.substring(0, 35) + "~";
            _canvas.drawCenterString(us.c_str(), 120, 70);
            OsUI::drawProgressBar(_canvas, _loadPct);

        } else if (_lineCount > 0) {
            // Covers both a real fetched page (_isReading) AND the error/
            // blocked/empty notice set up in _navigate() - those used to be
            // computed and then never actually shown, because this branch
            // used to require _isReading, which is false in exactly those
            // cases. Now anything with lines to show gets shown.
            //
            // Row height is no longer fixed: a heading line (PE_LF_BIG, see
            // paper_engine.h) renders at 2x text size and takes a taller
            // row, so how many lines actually fit varies by page - real
            // variable-height layout instead of a uniform text grid, just
            // computed fresh each frame instead of kept in a layout tree
            // (which wouldn't fit this board's RAM - see paper_engine.h).
            const int LINE_H = 13;
            const int BIG_H  = 22;
            const int bottomLimit = SCREEN_H - FOOTER_H - 4;
            uint16_t noticeColor = COL_TEXT;
            if (!_isReading) {
                if (_isBlocked)                  noticeColor = COL_WARN;
                else if (_lastStatusCode == 200) noticeColor = COL_DIM;   // "page had no text"
                else                             noticeColor = PaperEngine::codeColor(_lastStatusCode);
            }
            int y = CONTENT_Y + 2;
            int idx = _scrollPos;
            while (idx < _lineCount) {
                uint8_t flags = _lineFlags[idx];
                bool big    = flags & PE_LF_BIG;
                bool center = flags & PE_LF_CENTER;
                int rowH = big ? BIG_H : LINE_H;
                if (y + rowH > bottomLimit) break;

                _canvas.setTextSize(big ? 2 : 1);
                String& line = _dispLines[idx];

                int cursorX = 2;
                if (center) {
                    int visLen = 0;
                    for (int j = 0; j < (int)line.length(); j++) {
                        char c = line[j];
                        if (c == '\x01' || c == '\x03' || c == '\x07') { j++; continue; }
                        if (c == '\x02' || c == '\x04' || c == '\x05' || c == '\x06' || c == '\x08') continue;
                        visLen++;
                    }
                    int charW = big ? 12 : 6;
                    cursorX = max(2, (SCREEN_W - visLen * charW) / 2);
                }

                bool boldOn = false;
                _canvas.setTextColor(noticeColor, COL_BG);
                _canvas.setCursor(cursorX, y);
                for (int j = 0; j < (int)line.length(); j++) {
                    char c = line[j];
                    if (c == '\x01') {           // link start
                        j++;
                        if (j < (int)line.length()) {
                            int lid = (uint8_t)line[j] - 32;
                            bool sel = _isSelected(PaperEngine::EK_LINK, lid);
                            _canvas.setTextColor(sel ? (uint16_t)BLACK : (uint16_t)COL_ACCENT,
                                                 sel ? (uint16_t)COL_ACCENT : (uint16_t)COL_BG);
                        }
                    } else if (c == '\x02') {    // link end
                        _canvas.setTextColor(noticeColor, COL_BG);
                    } else if (c == '\x03') {    // form field start
                        j++;
                        if (j < (int)line.length()) {
                            int fid = (uint8_t)line[j] - 32;
                            bool sel = _isSelected(PaperEngine::EK_FIELD, fid);
                            _canvas.setTextColor(sel ? (uint16_t)BLACK : (uint16_t)COL_FIELD,
                                                 sel ? (uint16_t)COL_FIELD : (uint16_t)COL_BG);
                        }
                    } else if (c == '\x04') {    // field end
                        _canvas.setTextColor(noticeColor, COL_BG);
                    } else if (c == '\x05') {    // bold on
                        boldOn = true;
                    } else if (c == '\x06') {    // bold off
                        boldOn = false;
                    } else if (c == '\x07') {    // custom color on (from CSS, see paper_engine.h)
                        j++;
                        if (j < (int)line.length()) {
                            int pid = (uint8_t)line[j] - 32;
                            _canvas.setTextColor(PaperEngine::paletteColor(pid), COL_BG);
                        }
                    } else if (c == '\x08') {    // custom color off
                        _canvas.setTextColor(noticeColor, COL_BG);
                    } else if (boldOn) {
                        // No real bold font asset fits this board's flash
                        // budget - this is the classic embedded fake-bold:
                        // draw the glyph twice, 1px apart, then snap the
                        // cursor to where a single draw would've landed so
                        // bold text doesn't visually drift wider than it
                        // should over a long run.
                        int x0 = _canvas.getCursorX();
                        _canvas.print(c);
                        int x1 = _canvas.getCursorX();
                        _canvas.setCursor(x0 + 1, y);
                        _canvas.print(c);
                        _canvas.setCursor(x1, y);
                    } else {
                        _canvas.print(c);
                    }
                }
                y += rowH;
                idx++;
            }
            _canvas.setTextSize(1);
            _canvas.setTextColor(COL_TEXT, COL_BG);

            // Scroll bar
            if (_maxScroll > 0) {
                int sbH = SCREEN_H - HEADER_H - FOOTER_H;
                int sbY = HEADER_H + (_scrollPos * sbH / max(1, _maxScroll));
                sbY = constrain(sbY, HEADER_H, SCREEN_H - FOOTER_H - 8);
                _canvas.fillRect(SCREEN_W - 3, sbY, 2, 8, COL_DIM);
            }
        } else if (!_isLoading) {
            // Start page
            _canvas.setTextColor(COL_ACCENT, COL_BG);
            _canvas.drawCenterString("PaperWeb v" BR_VERSION, 120, 36);
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.drawCenterString("by " BR_AUTHOR, 120, 50);
            _canvas.drawFastHLine(40, 62, 160, COL_SELECTED);
            _canvas.drawCenterString("ENT = open URL / search", 120, 70);
            _canvas.drawCenterString("W=w3txt  K=bkm  T=tabs", 120, 83);
            _canvas.drawCenterString("H = history   ? = help", 120, 96);
            if (!WiFiManager::get().isConnected()) {
                _canvas.setTextColor(COL_ERR, COL_BG);
                _canvas.drawCenterString("! No WiFi - TAB on launcher", 120, 109);
            }
        }

        // Small "T2/3"-style tab indicator, tacked onto whichever footer
        // text is shown below - always visible so switching (T/1/2/3)
        // never leaves you guessing which tab you're on.
        String tabTag = " T" + String(_curTab + 1) + "/" + String(BR_MAX_TABS);

        // Footer
        if (_isReading) {
            String linfo = (_engine.selectableCount > 0)
                ? " [" + String(_selLink + 1) + "/" + String(_engine.selectableCount) + "]"
                : "";
            String foot = ";.=scrl ,//=nav ?=help" + linfo + tabTag;
            OsUI::drawFooter(_canvas, foot.c_str());
        } else if (_isLoading) {
            OsUI::drawFooter(_canvas, "Loading... please wait");
        } else {
            String foot = "ENT=search  ?=help" + tabTag;
            OsUI::drawFooter(_canvas, foot.c_str());
        }

        _canvas.pushSprite(0, 0);
    }

    // True if the given (kind, id) pair is the currently-selected item -
    // used to highlight exactly one link/field at a time regardless of
    // which array it actually lives in.
    bool _isSelected(PaperEngine::SelKind kind, int id) {
        return _selLink < _engine.selectableCount &&
               _engine.selectables[_selLink].kind == kind &&
               _engine.selectables[_selLink].idx  == id;
    }
};

Browser* Browser::_instance = nullptr;
