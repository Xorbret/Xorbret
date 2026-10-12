#pragma once
#include <M5Cardputer.h>
#include <SD.h>
#include <Preferences.h>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceID3.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../config.h"
#include "../../libs/AudioOutputM5Speaker.h"

// Music app: Core1 decode, SD read-ahead buffer (AdvanceOS-style ID3 chain).

static constexpr uint8_t  MUSIC_VIRTUAL_CH = 0;
static constexpr size_t   MUSIC_SD_BUF_SZ  = 32768;

static void _bufStatus(void*, int code, const char*) {
    (void)code;
}

static void _prefetchBuf(AudioFileSourceBuffer* buf) {
    uint32_t t0 = millis();
    while (buf->getFillLevel() < MUSIC_SD_BUF_SZ - 512 && millis() - t0 < 800) {
        buf->loop();
        vTaskDelay(1);
    }
}

static AudioOutputM5Speaker _musicOut(&M5.Speaker, MUSIC_VIRTUAL_CH);

struct MusicDecodeState {
    AudioFileSourceSD      sdFile;
    AudioFileSourceBuffer* buf       = nullptr;
    AudioFileSourceID3*    id3       = nullptr;
    AudioGeneratorMP3      mp3;
    AudioGeneratorWAV      wav;
    uint8_t                curType   = 0; // 0=WAV 1=MP3

    volatile bool playing   = false;
    volatile bool paused    = false;
    volatile bool trackEnd  = false;
    volatile bool doStop    = false;
    volatile bool doPlay    = false;

    char    nextFile[128];
    uint8_t nextType = 0;

    TaskHandle_t taskHandle = nullptr;
};

static MusicDecodeState _dec;

static void _decodeTask(void*) {
    while (true) {
        if (_dec.doStop) {
            if (_dec.curType == 1) _dec.mp3.stop();
            else                   _dec.wav.stop();
            if (_dec.id3) {
                _dec.id3->close();
                delete _dec.id3; _dec.id3 = nullptr;
            }
            if (_dec.buf) { delete _dec.buf; _dec.buf = nullptr; }
            _dec.sdFile.close();
            _musicOut.stop();
            _dec.playing = false;
            _dec.paused  = false;
            _dec.doStop  = false;
            vTaskDelay(1 / portTICK_PERIOD_MS);
            continue;
        }

        if (_dec.doPlay) {
            if (_dec.id3) {
                if (_dec.curType == 1) _dec.mp3.stop();
                else                   _dec.wav.stop();
                _dec.id3->close();
                delete _dec.id3; _dec.id3 = nullptr;
            }
            if (_dec.buf) { delete _dec.buf; _dec.buf = nullptr; }
            _dec.sdFile.close();
            _musicOut.stop(); // reset output driver state -- natural track-end
                              // doesn't go through _cmdStop(), so without this
                              // the next track could start against a driver
                              // still in whatever state the last one left it

            _dec.curType = _dec.nextType;
            bool sdOk = _dec.sdFile.open(_dec.nextFile);
            uint32_t fsize = sdOk ? _dec.sdFile.getSize() : 0;
            (void)fsize;

            bool started = false;
            if (sdOk) {
                _dec.buf = new AudioFileSourceBuffer(&_dec.sdFile, MUSIC_SD_BUF_SZ);
                _dec.buf->RegisterStatusCB(_bufStatus, nullptr);
                _prefetchBuf(_dec.buf);

                if (_dec.curType == 1) {
                    _dec.id3 = new AudioFileSourceID3(_dec.buf);
                    started = _dec.mp3.begin(_dec.id3, &_musicOut);
                } else {
                    // WAV: buffer -> decoder (no ID3; ID3+buf breaks header seek)
                    started = _dec.wav.begin(_dec.buf, &_musicOut);
                    if (!started) {
                        delete _dec.buf;
                        _dec.buf = nullptr;
                        _dec.sdFile.close();
                        if (_dec.sdFile.open(_dec.nextFile)) {
                            _dec.id3 = new AudioFileSourceID3(&_dec.sdFile);
                            started = _dec.wav.begin(_dec.id3, &_musicOut);
                        }
                    }
                }
            }

            _dec.playing  = started;
            _dec.paused   = false;
            _dec.trackEnd = false;
            _dec.doPlay   = false;
            continue;
        }

        if (_dec.playing && !_dec.paused) {
            if (_dec.buf) _dec.buf->loop();

            bool running = (_dec.curType == 1)
                ? _dec.mp3.isRunning()
                : _dec.wav.isRunning();

            if (!running) {
                _dec.playing  = false;
                _dec.trackEnd = true;
            } else {
                int bursts = (_dec.curType == 0) ? 24 : 4;
                for (int i = 0; i < bursts; i++) {
                    if (_dec.curType == 1) {
                        if (!_dec.mp3.loop()) break;
                    } else {
                        if (!_dec.wav.loop()) break;
                    }
                }
            }
            taskYIELD();
        } else {
            vTaskDelay(2 / portTICK_PERIOD_MS);
        }
    }
}

// The file manager picks a specific track before launching Music (otherwise
// Music would just open on its list screen without playing what the user
// actually selected).
static char g_musicPendingFile[128] = "";

class MusicPlayer : public App {
public:
    MusicPlayer(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Music"; }
    const char* getIcon() const override { return "M"; }
    const char* getDesc() const override { return "WAV/MP3 player"; }

    void onStart() override {
        _sel = _count = 0;
        _vol      = 180;
        _hintPage = 0;
        _loop = _shuffle = false;

        M5.Speaker.begin();
        M5.Speaker.setVolume(_vol);

        if (_dec.taskHandle == nullptr) {
            xTaskCreatePinnedToCore(
                _decodeTask, "MusicDecode",
                20480,   // was 12288 -- MP3 (Helix) decode needs more stack than WAV, too small was causing a crash/hang on track transitions
                nullptr, 5,
                &_dec.taskHandle, 1
            );
        }
        if (SD.cardType() != CARD_NONE) { _loadMusicDir(); _scanFiles(); }

        // No files where we're currently pointed, and nobody handed us a
        // specific file to play (Files app -> "open with Music") - don't
        // make people discover the F key, just ask up front.
        if (_count == 0 && !g_musicPendingFile[0]) {
            _pickMusicDir();
        }

        if (g_musicPendingFile[0]) {
            for (int i = 0; i < _count; i++) {
                if (strcmp(_files[i], g_musicPendingFile) == 0) {
                    _cmdPlay(i);
                    break;
                }
            }
            g_musicPendingFile[0] = '\0';
        }

        _draw();
    }

    void onStop() override {
        _dec.doStop = true;
        uint32_t t0 = millis();
        while (_dec.playing && millis() - t0 < 500) delay(5);
        if (_dec.taskHandle) {
            vTaskDelete(_dec.taskHandle);
            _dec.taskHandle = nullptr;
        }
        M5.Speaker.end();
    }

    void update() override {
        M5Cardputer.update();
        auto ks = M5Cardputer.Keyboard.keysState();

        if (_dec.trackEnd) { _dec.trackEnd = false; _onTrackEnd(); return; }

        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            OsUI::waitRelease(); _cmdStop(); AppManager::get().goHome(); return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('f')) { OsUI::waitRelease(); _pickMusicDir(); OsUI::waitRelease(); _draw(); return; }
        if (_count == 0) { _draw(); delay(50); return; }

        if (ks.enter) {
            OsUI::waitRelease();
            if (!_dec.playing) _cmdPlay(_sel);
            else _dec.paused = !_dec.paused;
            _draw(); return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('/')) { OsUI::waitRelease(); _navTrack(1);  return; }
        if (M5Cardputer.Keyboard.isKeyPressed(',')) { OsUI::waitRelease(); _navTrack(-1); return; }
        if (M5Cardputer.Keyboard.isKeyPressed(';')) { if (_sel > 0) _sel--;            delay(80); _draw(); return; }
        if (M5Cardputer.Keyboard.isKeyPressed('.')) { if (_sel < _count-1) _sel++;     delay(80); _draw(); return; }

        if (M5Cardputer.Keyboard.isKeyPressed('=') || M5Cardputer.Keyboard.isKeyPressed('+')) {
            _vol = min(255, _vol+8); M5.Speaker.setVolume(_vol); delay(40); _draw(); return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('-')) {
            _vol = max(0, _vol-8);  M5.Speaker.setVolume(_vol); delay(40); _draw(); return;
        }

        // Seek on SD; buffer cache follows sdFile position.
        if (M5Cardputer.Keyboard.isKeyPressed(']') && _dec.playing) {
            uint32_t sz = _dec.sdFile.getSize();
            uint32_t np = _dec.sdFile.getPos() + sz / 60;
            if (np > sz) np = sz;
            _dec.sdFile.seek(np, SEEK_SET);
            if (_dec.buf) _dec.buf->seek(np, SEEK_SET);
            delay(80); _draw(); return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('[') && _dec.playing) {
            uint32_t sz = _dec.sdFile.getSize();
            int32_t  np = (int32_t)_dec.sdFile.getPos() - (int32_t)(sz / 60);
            if (np < 0) np = 0;
            _dec.sdFile.seek((uint32_t)np, SEEK_SET);
            if (_dec.buf) _dec.buf->seek((uint32_t)np, SEEK_SET);
            delay(80); _draw(); return;
        }

        for (char c = '1'; c <= '5'; c++) {
            if (M5Cardputer.Keyboard.isKeyPressed(c)) {
                _musicOut.MyMusicEqualizer = c - '1';
                OsUI::waitRelease(); _draw(); return;
            }
        }
        if (M5Cardputer.Keyboard.isKeyPressed('l')) { _loop=!_loop; _shuffle=false; OsUI::waitRelease(); _draw(); return; }
        if (M5Cardputer.Keyboard.isKeyPressed('h')) { _shuffle=!_shuffle; _loop=false; OsUI::waitRelease(); _draw(); return; }
        if (ks.tab) { _hintPage=(_hintPage+1)%2; OsUI::waitRelease(); _draw(); return; }

        static uint32_t lastDraw = 0;
        uint32_t drawMs = _dec.playing ? 500 : 250;
        if (millis() - lastDraw > drawMs) { lastDraw = millis(); _draw(); }
    }

private:
    LGFX_Sprite& _canvas;
    static const int MAX_TRACKS = 128;
    char    _files[MAX_TRACKS][128];
    char    _names[MAX_TRACKS][36];
    uint8_t _type[MAX_TRACKS];
    int _count=0, _sel=0, _vol=180, _hintPage=0;
    String _musicDir = "/MitamaOS/music"; // overridden by _loadMusicDir()
    bool _loop=false, _shuffle=false;

    void _cmdPlay(int idx) {
        if (idx < 0 || idx >= _count) return;
        _sel = idx;
        strncpy(_dec.nextFile, _files[idx], 127); _dec.nextFile[127]=0;
        _dec.nextType = _type[idx];
        _dec.doPlay   = true;
    }
    void _cmdStop() {
        _dec.doStop = true;
        uint32_t t0 = millis();
        while (_dec.playing && millis()-t0 < 300) delay(5);
    }
    void _navTrack(int dir) {
        bool was = _dec.playing; _cmdStop();
        if (_shuffle && _count>1) { int n=_sel; while(n==_sel) n=random(0,_count); _sel=n; }
        else _sel = (_sel+dir+_count)%_count;
        if (was) _cmdPlay(_sel);
        _draw();
    }
    void _onTrackEnd() {
        if (_loop) { _cmdPlay(_sel); return; }
        if (_shuffle && _count>1) { int n=_sel; while(n==_sel) n=random(0,_count); _sel=n; }
        else _sel = (_sel+1)%_count;
        _cmdPlay(_sel);
        _draw();
    }

    void _loadMusicDir() {
        Preferences p;
        p.begin("music", true);
        String saved = p.getString("dir", "");
        p.end();
        if (saved.length() && SD.exists(saved.c_str())) _musicDir = saved;
    }

    void _saveMusicDir() {
        Preferences p;
        p.begin("music", false);
        p.putString("dir", _musicDir);
        p.end();
    }

    void _scanFiles() {
        _count = 0;
        if (!SD.exists("/MitamaOS")) SD.mkdir("/MitamaOS");
        if (!SD.exists(_musicDir.c_str())) SD.mkdir(_musicDir.c_str());
        File dir = SD.open(_musicDir.c_str());
        if (!dir || !dir.isDirectory()) return;
        File f = dir.openNextFile();
        while (f && _count < MAX_TRACKS) {
            if (!f.isDirectory()) {
                String n=String(f.name()), nl=n; nl.toLowerCase();
                uint8_t t=255;
                if (nl.endsWith(".wav")) t=0;
                if (nl.endsWith(".mp3")) t=1;
                if (t!=255) {
                    String fp = n.startsWith("/") ? n : _musicDir+"/"+n;
                    int sl=fp.lastIndexOf('/');
                    String base=(sl>=0)?fp.substring(sl+1):fp;
                    strncpy(_files[_count],fp.c_str(),127); _files[_count][127]=0;
                    strncpy(_names[_count],base.c_str(),35); _names[_count][35]=0;
                    _type[_count]=t; _count++;
                }
            }
            f.close(); f=dir.openNextFile();
        }
        dir.close();
    }

    // Simple directory-only browser so the music folder isn't hardcoded --
    // lets people point Music at whatever folder they already use with
    // other Cardputer firmwares instead of duplicating their library into
    // /MitamaOS/music. `-`=up a level, ENT=go in/pick this folder, `=cancel.
    void _pickMusicDir() {
        String cur = _musicDir;
        while (true) {
            String names[64]; int n = 0;
            File dir = SD.open(cur.c_str());
            if (dir && dir.isDirectory()) {
                File f = dir.openNextFile();
                while (f && n < 64) {
                    if (f.isDirectory()) names[n++] = String(f.name());
                    f.close(); f = dir.openNextFile();
                }
                dir.close();
            }
            // simple bubble sort, n is tiny (<=64), fine to keep this dumb
            for (int i=0;i<n;i++) for (int j=i+1;j<n;j++) if (names[j]<names[i]) { String t=names[i]; names[i]=names[j]; names[j]=t; }

            int sel = 0, camY = 0;
            const int ROW_H = 15, VISIBLE = 6;
            const int LIST_Y = CONTENT_Y + 16;
            const int LSTH = VISIBLE * ROW_H;

            while (true) {
                _canvas.fillSprite(COL_BG);
                OsUI::drawHeader(_canvas, "Pick Music Folder", COL_HEADER);
                _canvas.setTextColor(COL_ACCENT, COL_BG);
                char cb[40]; strncpy(cb, cur.c_str(), 39); cb[39]=0;
                _canvas.setCursor(4, CONTENT_Y + 2); _canvas.print(cb);
                OsUI::drawFooter(_canvas, "-=up ENT=open/pick `=cancel");

                if (n == 0) {
                    _canvas.setTextColor(COL_DIM, COL_BG);
                    _canvas.drawCenterString("(no subfolders - ENT picks this)", SCREEN_W/2, LIST_Y + 10);
                } else {
                    if (sel*ROW_H < -camY) camY = -sel*ROW_H;
                    else if (sel*ROW_H > -camY+(VISIBLE-1)*ROW_H) camY = -((sel-VISIBLE+1)*ROW_H);
                    camY = constrain(camY, -(max(0, n-VISIBLE)*ROW_H), 0);

                    _canvas.setClipRect(0, LIST_Y, SCREEN_W, LSTH);
                    for (int i=0;i<n;i++) {
                        int y = LIST_Y + i*ROW_H + camY;
                        if (y + ROW_H < LIST_Y || y > LIST_Y + LSTH) continue;
                        bool rowSel = (i==sel);
                        uint16_t bg = rowSel ? COL_SELECTED : COL_BG;
                        _canvas.fillRect(0, y, SCREEN_W, ROW_H-1, bg);
                        _canvas.setTextColor(rowSel ? WHITE : COL_TEXT, bg);
                        char nb[30]; strncpy(nb, names[i].c_str(), 29); nb[29]=0;
                        _canvas.setCursor(8, y+3); _canvas.print(nb);
                    }
                    _canvas.clearClipRect();
                    if (n > VISIBLE) {
                        int barH = max(4, VISIBLE*VISIBLE*ROW_H/n);
                        int barY = LIST_Y + ((-camY)*(LSTH-barH))/max(1,(n-VISIBLE)*ROW_H);
                        _canvas.fillRect(SCREEN_W-3, LIST_Y, 3, LSTH, COL_BG2);
                        _canvas.fillRect(SCREEN_W-3, barY, 3, barH, COL_ACCENT);
                    }
                }
                _canvas.pushSprite(0,0);

                bool nav = false;
                while (!nav) {
                    M5Cardputer.update();
                    auto st = M5Cardputer.Keyboard.keysState();
                    if (n>0 && M5Cardputer.Keyboard.isKeyPressed(';')) { if (sel>0) sel--; delay(80); nav=true; }
                    if (n>0 && M5Cardputer.Keyboard.isKeyPressed('.')) { if (sel<n-1) sel++; delay(80); nav=true; }
                    if (M5Cardputer.Keyboard.isKeyPressed('-')) {
                        OsUI::waitRelease();
                        if (cur != "/") { int sl=cur.lastIndexOf('/'); cur = (sl<=0)?"/":cur.substring(0,sl); }
                        nav=true; goto reload;
                    }
                    if (st.enter) {
                        OsUI::waitRelease();
                        if (n==0) {
                            // nothing to navigate into - this IS the
                            // folder the user wants, so ENT picks it
                            // (footer already promises "ENT=open/pick")
                            _musicDir = cur;
                            _saveMusicDir();
                            _scanFiles();
                            _sel = 0;
                            OsUI::showToast(_canvas, "Folder set", COL_OK);
                            return;
                        }
                        String next = cur; if (!next.endsWith("/")) next += "/"; next += names[sel];
                        cur = next;
                        nav=true; goto reload;
                    }
                    if (M5Cardputer.Keyboard.isKeyPressed('`')) { OsUI::waitRelease(); return; }
                    delay(10);
                }
            }
            reload:;
            // pressing ENT with nothing selected / on the current folder
            // itself would need a distinct "pick this" action -- use Tab
            // to accept the currently shown folder as the music dir
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.keysState().tab) {
                OsUI::waitRelease();
                _musicDir = cur;
                _saveMusicDir();
                _scanFiles();
                _sel = 0;
                OsUI::showToast(_canvas, "Folder set", COL_OK);
                return;
            }
        }
    }

    float _progress() {
        if (!_dec.playing) return 0.f;
        uint32_t sz = _dec.sdFile.getSize();
        return sz ? (float)_dec.sdFile.getPos()/sz : 0.f;
    }

    void _draw() {
        _canvas.fillSprite(COL_BG);
        static const char* eqN[]={"FLAT","WARM","BASS","TRBL","VOIC"};
        int eq=_musicOut.MyMusicEqualizer;
        const char* st=_dec.paused?"PAUSE":(_dec.playing?"PLAY ":"STOP ");
        char hbuf[36]; snprintf(hbuf,sizeof(hbuf),"Music  %s  %s",st,eqN[eq<5?eq:0]);
        OsUI::drawHeader(_canvas,hbuf,COL_HEADER);

        if (_count==0) {
            _canvas.setTextColor(COL_DIM,COL_BG); _canvas.setTextSize(1);
            char nb[48]; snprintf(nb,sizeof(nb),"No files in %s",_musicDir.c_str());
            _canvas.drawCenterString(nb,120,54);
            _canvas.drawCenterString(".wav and .mp3 supported",120,68);
            OsUI::drawFooter(_canvas,"F=folder  ENT=play  `=back");
            _canvas.pushSprite(0,0); return;
        }

        float prog=_progress();
        _canvas.fillRect(0,CONTENT_Y,SCREEN_W,3,COL_BG2);
        if (_dec.playing) {
            int pw=(int)(prog*SCREEN_W);
            _canvas.fillRect(0,CONTENT_Y,pw,3,_dec.paused?(uint16_t)COL_WARN:(uint16_t)COL_OK);
        }

        const int ROW_H=13, VISIBLE=6, LIST_Y=CONTENT_Y+5;
        int scroll=max(0,min(_sel-VISIBLE/2,_count-VISIBLE)); if(scroll<0)scroll=0;
        for (int i=scroll; i<_count && i<scroll+VISIBLE; i++) {
            bool cur=(i==_sel);
            int y=LIST_Y+(i-scroll)*ROW_H;
            uint16_t bg=cur?(uint16_t)COL_SELECTED:(uint16_t)COL_BG;
            uint16_t fg=cur?(uint16_t)TFT_WHITE:(uint16_t)COL_TEXT;
            _canvas.fillRect(0,y,SCREEN_W,ROW_H-1,bg);
            if (cur&&_dec.playing) {
                _canvas.setTextColor(_dec.paused?(uint16_t)COL_WARN:(uint16_t)COL_OK,bg);
                _canvas.setCursor(2,y+2); _canvas.print(_dec.paused?"||":" >");
            } else {
                _canvas.setTextColor(_type[i]==1?(uint16_t)COL_OK:(uint16_t)COL_ACCENT,bg);
                _canvas.setCursor(2,y+2); _canvas.print(_type[i]==1?"M3":"WV");
            }
            char buf[32]; strncpy(buf,_names[i],31); buf[31]=0;
            int len=strlen(buf); if(len>4&&buf[len-4]=='.')buf[len-4]=0;
            _canvas.setTextColor(fg,bg); _canvas.setCursor(20,y+2); _canvas.print(buf);
        }
        if (_count>VISIBLE) {
            int th=VISIBLE*ROW_H, bh=max(3,th*VISIBLE/_count);
            int by=LIST_Y+(scroll*th)/_count;
            _canvas.fillRect(SCREEN_W-2,LIST_Y,2,th,COL_BG2);
            _canvas.fillRect(SCREEN_W-2,by,2,bh,COL_ACCENT);
        }

        int infoY=SCREEN_H-FOOTER_H-12;
        _canvas.setTextColor(COL_DIM,COL_BG);
        _canvas.setCursor(2,infoY); _canvas.print("VOL");
        int vbw=map(_vol,0,255,0,60);
        _canvas.drawRect(22,infoY,62,7,COL_DIM);
        _canvas.fillRect(23,infoY+1,vbw,5,_vol>200?(uint16_t)COL_WARN:(uint16_t)COL_ACCENT);
        _canvas.setCursor(90,infoY);
        if(_loop) _canvas.print("LOOP"); else if(_shuffle) _canvas.print("SHFL"); else _canvas.print(" SEQ");
        char cnt[10]; snprintf(cnt,sizeof(cnt),"%d/%d",_sel+1,_count);
        _canvas.setTextColor(COL_DIM,COL_BG);
        _canvas.setTextDatum(top_right); _canvas.drawString(cnt,SCREEN_W-4,infoY); _canvas.setTextDatum(top_left);

        static const char* hints[]={"ENT=ply ,/;/.=nav [/]=rwd +/-=vol","1-5=EQ L=loop H=shfl F=fldr Tab=hints"};
        OsUI::drawFooter(_canvas,hints[_hintPage]);
        _canvas.pushSprite(0,0);
    }
};
