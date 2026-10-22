#pragma once
#include <Arduino.h>
#include <SD.h>
#include <math.h>
#include <vector>
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "model_flash.h"

#define VOCAB       256
#define N_EMBD      192
#define N_LAYER     12
#define N_HEAD      6
#define HEAD_DIM    32
#define CTX_LEN     256
#define KV_MAX_POS  96
#define FF_DIM      (4*N_EMBD)
#define N_TENSORS   125   // 2 + 12*10 + 3

// The defines above are the largest model the buffers hold. The model
// file's header sets the actual size (any width that's a multiple of 32
// up to 192, 1-12 layers), so a smaller, faster model needs no rebuild.
static int g_embd = N_EMBD, g_layers = N_LAYER, g_heads = N_HEAD, g_ff = FF_DIM, g_ntensors = N_TENSORS;

static uint32_t g_off[N_TENSORS];
static float    g_sc[N_TENSORS];
static bool     g_model_ok = false;

static float  x_buf[N_EMBD];
static float  res_buf[N_EMBD];
static float  ln_out[N_EMBD];
static float  ln_w[N_EMBD], ln_b[N_EMBD];
static float  q_buf[N_EMBD];
static float  k_buf[HEAD_DIM];
static float  v_buf[HEAD_DIM];
static float  attn_out[N_EMBD];
static float  ff_buf[FF_DIM];
static float  logits_buf[VOCAB];
static int8_t row_buf[FF_DIM];
static float  scores[KV_MAX_POS];
static float  k_t[HEAD_DIM], v_t[HEAD_DIM];

// The key/value cache is ~74 KB. Allocated one layer at a time (6 KB
// each), so it fits a fragmented heap that has no single 74 KB block.
static int8_t *kv_layer[N_LAYER] = {};
// SD path: read many rows per call. Each File.read() has a fixed cost, so
// 16 KB at a time is much faster than one 192-byte row at a time.
static int8_t* g_sdbuf = nullptr;
static size_t  g_sdbuf_n = 0;
static constexpr size_t SDBUF = 16384;
static float  *kv_scale = nullptr;
static constexpr size_t KV_LAYER_BYTES = (size_t)KV_MAX_POS * 2 * HEAD_DIM;

static inline size_t kv_slot(int layer, int pos, int kv) {
    return ((size_t)layer * KV_MAX_POS + pos) * 2 + kv;
}
static inline int8_t* kv_ptr(int layer, int pos, int kv) {
    return kv_layer[layer] + ((size_t)pos * 2 + kv) * HEAD_DIM;
}

static void kv_ram_free() {
    for (auto &p : kv_layer) { free(p); p = nullptr; }
    free(g_sdbuf); g_sdbuf = nullptr;
    free(kv_scale); kv_scale = nullptr;
}

static bool kv_ram_init() {
    size_t scale_n = (size_t)g_layers * KV_MAX_POS * 2;
    kv_scale = (float*)malloc(scale_n * sizeof(float));
    bool ok = kv_scale != nullptr;
    for (int l = 0; ok && l < g_layers; l++) ok = (kv_layer[l] = (int8_t*)calloc(KV_LAYER_BYTES, 1)) != nullptr;
    if (!ok) { Serial.println("KV malloc failed"); kv_ram_free(); return false; }
    // Optional: without it the SD path just reads row by row (slower).
    g_sdbuf_n = SDBUF;
    g_sdbuf = (int8_t*)malloc(g_sdbuf_n);
    if (!g_sdbuf) { g_sdbuf_n = SDBUF / 4; g_sdbuf = (int8_t*)malloc(g_sdbuf_n); }
    for (size_t i = 0; i < scale_n; i++) kv_scale[i] = 1.0f;
    Serial.printf("KV RAM OK: %.1f KB\n", (g_layers * KV_LAYER_BYTES + scale_n*4) / 1024.0f);
    return true;
}

static void kv_reset() {
    if (!kv_scale) return;
    for (int l = 0; l < g_layers; l++) memset(kv_layer[l], 0, KV_LAYER_BYTES);
    size_t n = (size_t)g_layers * KV_MAX_POS * 2;
    for (size_t i = 0; i < n; i++) kv_scale[i] = 1.0f;
}

static void kv_write(int layer, int pos, int kv_idx, const float *vec) {
    size_t slot = kv_slot(layer, pos, kv_idx);
    float sc = 1e-9f;
    for (int i = 0; i < HEAD_DIM; i++) { float a = fabsf(vec[i]); if (a > sc) sc = a; }
    sc /= 127.0f;
    kv_scale[slot] = sc;
    int8_t *dst = kv_ptr(layer, pos, kv_idx);
    for (int i = 0; i < HEAD_DIM; i++) {
        int v = (int)roundf(vec[i] / sc);
        dst[i] = v > 127 ? 127 : v < -127 ? -127 : (int8_t)v;
    }
}

static void kv_read(int layer, int pos, int kv_idx, float *out) {
    float sc = kv_scale[kv_slot(layer, pos, kv_idx)];
    const int8_t *src = kv_ptr(layer, pos, kv_idx);
    for (int i = 0; i < HEAD_DIM; i++) out[i] = src[i] * sc;
}

// Weights come from the flash copy when it's mapped (fast), else the SD
// card. g_cur is the read position in the mapped copy.
static const uint8_t* g_map = nullptr;
static volatile bool     _stop_requested = false;  // cooperative stop flag for a clean deinit()
static uint32_t       g_cur = 0;

static inline void mseek(File &f, uint32_t off) { if (g_map) g_cur = off; else f.seek(off); }

static void safe_read(File &f, void *buf, size_t len) {
    if (g_map) { memcpy(buf, g_map + g_cur, len); g_cur += len; return; }
    size_t got = f.read((uint8_t*)buf, len);
    if (got < len) memset((uint8_t*)buf + got, 0, len - got);
}

static void stream_vec(File &f, float *out, int dim, float sc) {
    safe_read(f, row_buf, dim);
    for (int i = 0; i < dim; i++) out[i] = row_buf[i] * sc;
}

static void stream_mv(File &f, float *out, const float *in, int rows, int cols, float sc) {
    if (!g_map && g_sdbuf) {
        int per = (int)(g_sdbuf_n / cols);
        for (int r0 = 0; r0 < rows; r0 += per) {
            int n = rows - r0 < per ? rows - r0 : per;
            safe_read(f, g_sdbuf, (size_t)n * cols);
            const int8_t* w = g_sdbuf;
            for (int i = 0; i < n; i++, w += cols) {
                float s = 0.0f;
                for (int j = 0; j < cols; j++) s += w[j] * in[j];
                out[r0 + i] = s * sc;
            }
            esp_task_wdt_reset();
        }
        return;
    }
    if (g_map) {
        const int8_t* w = (const int8_t*)(g_map + g_cur);
        for (int i = 0; i < rows; i++, w += cols) {
            float s = 0.0f;
            for (int j = 0; j < cols; j++) s += w[j] * in[j];
            out[i] = s * sc;
            if ((i & 0x7F) == 0) esp_task_wdt_reset();
        }
        g_cur += (uint32_t)rows * cols;
        return;
    }
    for (int i = 0; i < rows; i++) {
        safe_read(f, row_buf, cols);
        float s = 0.0f;
        for (int j = 0; j < cols; j++) s += row_buf[j] * in[j];
        out[i] = s * sc;
        if ((i & 0x1F) == 0) esp_task_wdt_reset();
    }
}

static void layernorm(float *out, const float *in, const float *w, const float *b) {
    float mean = 0.0f;
    for (int i = 0; i < g_embd; i++) mean += in[i];
    mean /= g_embd;
    float var = 0.0f;
    for (int i = 0; i < g_embd; i++) { float d = in[i]-mean; var += d*d; }
    float inv = 1.0f / sqrtf(var / g_embd + 1e-5f);
    for (int i = 0; i < g_embd; i++) out[i] = (in[i]-mean)*inv*w[i]+b[i];
}

static inline float gelu_f(float x) {
    return 0.5f*x*(1.0f+tanhf(0.7978845608f*(x+0.044715f*x*x*x)));
}

static bool model_init() {
    File f;
    if (!g_map) {
        f = SD.open("/MitamaOS/model.bin", "r");
        if (!f) { Serial.println("model.bin not found"); return false; }
    }
    Serial.printf("model_init: reading from %s\n", g_map ? "flash" : "SD");
    mseek(f, 0);
    uint32_t hdr[6]; safe_read(f, hdr, 24);
    bool ok = hdr[0]==VOCAB && hdr[1] % HEAD_DIM == 0 && hdr[1] >= HEAD_DIM && hdr[1] <= N_EMBD &&
              hdr[2] == hdr[1] / HEAD_DIM && hdr[3] >= 1 && hdr[3] <= N_LAYER &&
              hdr[4] == CTX_LEN && hdr[5] == 2 + 10 * hdr[3] + 3;
    if (ok) { g_embd = hdr[1]; g_heads = hdr[2]; g_layers = hdr[3]; g_ff = 4 * g_embd; g_ntensors = hdr[5]; }
    if (!ok) {
        Serial.printf("Model hdr mismatch: vocab=%u embd=%u head=%u layer=%u tensors=%u\n",
                      hdr[0],hdr[1],hdr[2],hdr[3],hdr[5]);
        if (f) f.close();
        return false;
    }
    // Each tensor must be the size its shape says and lie inside the file:
    // a cut-off model.bin would otherwise send reads past the end (in
    // flash, past the mapping: a crash on every open).
    const uint32_t total = g_map ? ModelFlash::mapSize() : (uint32_t)f.size();
    auto expect = [&](int i) -> uint32_t {
        const uint32_t E = g_embd;
        if (i == 0) return VOCAB * E;
        if (i == 1) return CTX_LEN * E;
        if (i == g_ntensors - 1) return VOCAB * E;
        if (i >= g_ntensors - 3) return E;
        switch ((i - 2) % 10) {
            case 2: case 5: return E * E;
            case 3: case 4: return HEAD_DIM * E;
            case 8: case 9: return (uint32_t)g_ff * E;
            default:        return E;
        }
    };
    uint32_t pos = 24;
    for (int i = 0; i < g_ntensors; i++) {
        if (pos + 4 > total) { ok = false; break; }
        uint32_t sz; safe_read(f, &sz, 4);
        pos += 4;
        if (sz < expect(i) || sz > total - pos) { ok = false; break; }
        g_off[i] = pos;
        pos += sz;
        mseek(f, pos);
    }
    if (ok && pos + 4u * g_ntensors > total) ok = false;
    if (!ok) {
        Serial.println("model.bin is cut short or damaged");
        if (f) f.close();
        return false;
    }
    for (int i = 0; i < g_ntensors; i++) safe_read(f, &g_sc[i], 4);
    if (f) f.close();
    Serial.println("model_init OK");
    return true;
}

// tensor order: tok_emb, pos_emb,
// per layer: ln1.w ln1.b Q[192x192] K[32x192] V[32x192] proj[192x192] ln2.w ln2.b ff1[768x192] ff2[192x768]
// ln_f.w ln_f.b head[256x192]
static void model_forward(File &mf, int tok, int pos) {
    int ti = 0;
    int ctx = (pos + 1 < KV_MAX_POS) ? pos + 1 : KV_MAX_POS;
    int slot = pos % KV_MAX_POS;

    mseek(mf, g_off[ti] + (uint32_t)tok * g_embd);
    stream_vec(mf, x_buf, g_embd, g_sc[ti]); ti++;

    mseek(mf, g_off[ti] + (uint32_t)(pos % CTX_LEN) * g_embd);
    stream_vec(mf, res_buf, g_embd, g_sc[ti]); ti++;
    for (int i = 0; i < g_embd; i++) x_buf[i] += res_buf[i];

    for (int l = 0; l < g_layers; l++) {
        if (_stop_requested) return;   // leaving MiTalk: don't finish a ~4 s pass from SD
        mseek(mf, g_off[ti]); stream_vec(mf, ln_w, g_embd, g_sc[ti]); ti++;
        mseek(mf, g_off[ti]); stream_vec(mf, ln_b, g_embd, g_sc[ti]); ti++;
        layernorm(ln_out, x_buf, ln_w, ln_b);

        mseek(mf, g_off[ti]); stream_mv(mf, q_buf, ln_out, g_embd,     g_embd, g_sc[ti]); ti++;
        mseek(mf, g_off[ti]); stream_mv(mf, k_buf, ln_out, HEAD_DIM,   g_embd, g_sc[ti]); ti++;
        mseek(mf, g_off[ti]); stream_mv(mf, v_buf, ln_out, HEAD_DIM,   g_embd, g_sc[ti]); ti++;

        kv_write(l, slot, 0, k_buf);
        kv_write(l, slot, 1, v_buf);

        const float inv_scale = 1.0f / sqrtf((float)HEAD_DIM);
        memset(attn_out, 0, g_embd * sizeof(float));

        for (int h = 0; h < g_heads; h++) {
            int hoff = h * HEAD_DIM;
            float mx = -1e9f;
            for (int t = 0; t < ctx; t++) {
                kv_read(l, t % KV_MAX_POS, 0, k_t);
                float dot = 0.0f;
                for (int i = 0; i < HEAD_DIM; i++) dot += q_buf[hoff+i] * k_t[i];
                scores[t] = dot * inv_scale;
                if (scores[t] > mx) mx = scores[t];
            }
            float sum = 0.0f;
            for (int t = 0; t < ctx; t++) { scores[t] = expf(scores[t]-mx); sum += scores[t]; }
            float inv2 = (sum > 1e-12f) ? 1.0f/sum : 1.0f;
            for (int t = 0; t < ctx; t++) scores[t] *= inv2;
            for (int t = 0; t < ctx; t++) {
                kv_read(l, t % KV_MAX_POS, 1, v_t);
                float s = scores[t];
                for (int i = 0; i < HEAD_DIM; i++) attn_out[hoff+i] += s * v_t[i];
            }
        }

        mseek(mf, g_off[ti]); stream_mv(mf, res_buf, attn_out, g_embd, g_embd, g_sc[ti]); ti++;
        for (int i = 0; i < g_embd; i++) x_buf[i] += res_buf[i];

        mseek(mf, g_off[ti]); stream_vec(mf, ln_w, g_embd, g_sc[ti]); ti++;
        mseek(mf, g_off[ti]); stream_vec(mf, ln_b, g_embd, g_sc[ti]); ti++;
        layernorm(ln_out, x_buf, ln_w, ln_b);

        mseek(mf, g_off[ti]); stream_mv(mf, ff_buf, ln_out, g_ff, g_embd, g_sc[ti]); ti++;
        for (int i = 0; i < g_ff; i++) {
            ff_buf[i] = gelu_f(ff_buf[i]);
            if ((i & 0x3F) == 0) esp_task_wdt_reset();
        }
        mseek(mf, g_off[ti]); stream_mv(mf, res_buf, ff_buf, g_embd, g_ff, g_sc[ti]); ti++;
        for (int i = 0; i < g_embd; i++) x_buf[i] += res_buf[i];
    }

    mseek(mf, g_off[ti]); stream_vec(mf, ln_w, g_embd, g_sc[ti]); ti++;
    mseek(mf, g_off[ti]); stream_vec(mf, ln_b, g_embd, g_sc[ti]); ti++;
    layernorm(ln_out, x_buf, ln_w, ln_b);
    mseek(mf, g_off[ti]); stream_mv(mf, logits_buf, ln_out, VOCAB, g_embd, g_sc[ti]);
}

static bool would_repeat_ngram(const std::vector<int> &r, int cand, int n=3) {
    int sz = r.size();
    if (sz < n-1) return false;
    for (int i = 0; i <= sz-(n-1); i++) {
        bool ok = true;
        for (int j = 0; j < n-1; j++) if (r[i+j]!=r[sz-(n-1)+j]) { ok=false; break; }
        if (ok && r[i+(n-1)]==cand) return true;
    }
    return false;
}

static inline bool is_printable(int id) { return (id>=32&&id<127)||id=='\n'; }

static int do_sample(float temp, int top_k, float top_p, float rep_pen,
                     const std::vector<int> &recent) {
    float tmp[VOCAB];
    for (int i = 0; i < VOCAB; i++) tmp[i] = logits_buf[i];
    for (int r : recent)
        if (r>=0&&r<VOCAB) tmp[r] = tmp[r]<0 ? tmp[r]*rep_pen : tmp[r]/rep_pen;
    float inv_t = temp > 1e-6f ? 1.0f/temp : 1e6f;
    for (int i = 0; i < VOCAB; i++) tmp[i] *= inv_t;
    int idx[VOCAB]; for (int i=0;i<VOCAB;i++) idx[i]=i;
    int k = top_k>0&&top_k<VOCAB ? top_k : VOCAB;
    for (int i=0;i<k;i++) {
        int best=i;
        for (int j=i+1;j<VOCAB;j++) if (tmp[idx[j]]>tmp[idx[best]]) best=j;
        int t=idx[i]; idx[i]=idx[best]; idx[best]=t;
    }
    float mx=tmp[idx[0]], sum=0; float probs[VOCAB]={};
    for (int i=0;i<k;i++) { float p=expf(tmp[idx[i]]-mx); probs[idx[i]]=p; sum+=p; }
    if (sum<=0) return idx[0];
    for (int i=0;i<k;i++) probs[idx[i]]/=sum;
    float cum=0;
    for (int i=0;i<k;i++) { cum+=probs[idx[i]]; if(cum>top_p){for(int j=i+1;j<k;j++) probs[idx[j]]=0;break;} }
    sum=0; for (int i=0;i<k;i++) sum+=probs[idx[i]];
    if (sum<=0) return idx[0];
    for (int i=0;i<k;i++) probs[idx[i]]/=sum;
    float r=(float)rand()/((float)RAND_MAX+1), acc=0;
    for (int i=0;i<k;i++) {
        int id=idx[i];
        if (probs[id]<=0||!is_printable(id)) continue;
        if (would_repeat_ngram(recent,id,3)) continue;
        acc+=probs[id]; if(r<=acc) return id;
    }
    for (int i=0;i<k;i++) if(is_printable(idx[i])) return idx[i];
    return idx[0];
}

static String ai_generate(File &mf, const String &prompt, int max_new) {
    kv_reset();
    String result; result.reserve(256);
    std::vector<int> recent; recent.reserve(128);

    // sampling params — TinyStories style: warmer temp for longer flowing text
    const float TEMP    = 0.85f;
    const int   TOP_K   = 40;
    const float TOP_P   = 0.92f;
    const float REP_PEN = 1.2f;
    const unsigned long TIMEOUT = 420000UL;  // 7 min: ~3.5 s per character from SD

    unsigned long t0 = millis();
    int pos = 0;

    // encode prompt
    for (int i = 0; i < (int)prompt.length() && pos < KV_MAX_POS-1; i++) {
        model_forward(mf, (uint8_t)prompt[i], pos);
        recent.push_back((uint8_t)prompt[i]);
        if ((int)recent.size() > 128) recent.erase(recent.begin());
        pos++;
        esp_task_wdt_reset();
        vTaskDelay(1);
    }

    int newlines = 0;
    for (int t = 0; t < max_new && pos < KV_MAX_POS-1; t++) {
        if (millis()-t0 > TIMEOUT) break;

        int tok = do_sample(TEMP, TOP_K, TOP_P, REP_PEN, recent);
        recent.push_back(tok);
        if ((int)recent.size() > 128) recent.erase(recent.begin());

        if (tok == 0) break;

        if (tok == '\n') {
            newlines++;
            result += '\n';
            if (newlines >= 2) break;  // double newline = end of answer
        } else {
            newlines = 0;
            if (tok >= 32 && tok < 127) result += (char)tok;
        }

        model_forward(mf, tok, pos);
        pos++;
        esp_task_wdt_reset();
        vTaskDelay(1);
    }

    // trim trailing newlines/spaces
    while (result.length() > 0 &&
           (result[result.length()-1]=='\n' || result[result.length()-1]==' '))
        result.remove(result.length()-1);

    return result.length() ? result : "?";
}

namespace MyAI {

static SemaphoreHandle_t _mutex       = nullptr;
static TaskHandle_t      _task_handle = nullptr;
static char              _prompt[128];
static char              _response[512];
static volatile bool     _request    = false;
static volatile bool     _done       = false;
static volatile bool     _busy       = false;
static bool              _started    = false;

// Live progress for the UI - without this the screen just shows a static
// "wait..." indistinguishable from a hang, even though the engine is
// actively computing (each token re-reads the full model weights from
// flash, see the comment in model_forward above - slow, but not stuck).
static volatile uint32_t _busy_since = 0;  // millis() when generation started, 0 = idle
static volatile int      _cur_pos   = 0;   // current position (prompt+generation)
static volatile int      _cur_phase = 0;   // 0=idle 1=encoding prompt 2=generating

// streaming token buffer — one char at a time
static char              _stream_char = 0;
static volatile bool     _stream_new  = false;  // set by AI task, cleared by app

static void ai_task(void*) {
    while (true) {
        if (_request) {
            char local[128];
            if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
                strcpy(local, _prompt);
                _request = false; _busy = true;
                _busy_since = millis(); _cur_pos = 0; _cur_phase = 0;
                _response[0] = '\0';
                xSemaphoreGive(_mutex);
            }
            File mf;
            if (!g_map) mf = SD.open("/MitamaOS/model.bin", "r");
            if (!g_map && !mf) {
                if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
                    strcpy(_response, "model.bin not found");
                    _done = true; _busy = false; _busy_since = 0; _cur_phase = 0;
                    xSemaphoreGive(_mutex);
                }
                vTaskDelay(10); continue;
            }

            kv_reset();
            String result; result.reserve(256);
            std::vector<int> recent; recent.reserve(128);
            const float TEMP    = 0.85f;
            const int   TOP_K   = 40;
            const float TOP_P   = 0.92f;
            const float REP_PEN = 1.2f;
            const unsigned long TIMEOUT = 420000UL;
            unsigned long t0 = millis();
            int pos = 0;

            // encode prompt (no streaming during prompt)
            _cur_phase = 1; _cur_pos = 0;
            for (int i = 0; i < (int)strlen(local) && pos < KV_MAX_POS-1; i++) {
                if (_stop_requested) break;
                model_forward(mf, (uint8_t)local[i], pos++);
                _cur_pos = pos;
                recent.push_back((uint8_t)local[i]);
                if ((int)recent.size() > 128) recent.erase(recent.begin());
                esp_task_wdt_reset();
                vTaskDelay(1);
            }

            int newlines = 0;
            _cur_phase = 2;
            for (int t = 0; t < KV_MAX_POS && pos < KV_MAX_POS-1; t++) {
                if (millis()-t0 > TIMEOUT) break;
                if (_stop_requested) break;
                int tok = do_sample(TEMP, TOP_K, TOP_P, REP_PEN, recent);
                recent.push_back(tok);
                if ((int)recent.size() > 128) recent.erase(recent.begin());
                if (tok == 0) break;
                if (tok == '\n') {
                    newlines++;
                    if (newlines >= 2) break;
                    result += '\n';
                } else {
                    newlines = 0;
                    if (tok >= 32 && tok < 127) {
                        char c = (char)tok;
                        result += c;
                        // emit token to stream buffer
                        // wait until app consumed previous
                        unsigned long tw = millis();
                        while (_stream_new && millis()-tw < 200) vTaskDelay(2);
                        if (xSemaphoreTake(_mutex, 10)) {
                            _stream_char = c;
                            _stream_new  = true;
                            xSemaphoreGive(_mutex);
                        }
                    }
                }
                model_forward(mf, tok, pos++);
                _cur_pos = pos;
                esp_task_wdt_reset();
                vTaskDelay(1);
            }
            if (mf) mf.close();

            // trim
            while (result.length() > 0 &&
                   (result[result.length()-1]=='\n' || result[result.length()-1]==' '))
                result.remove(result.length()-1);

            if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
                strncpy(_response, result.length() ? result.c_str() : "?", 511);
                _response[511] = '\0';
                _stream_new = false;
                _done = true; _busy = false; _busy_since = 0; _cur_phase = 0;
                xSemaphoreGive(_mutex);
            }
        }
        vTaskDelay(10);
    }
}

static const char* _last_error = "";

inline bool init() {
    if (_started) return g_model_ok;
    if (!_mutex) _mutex = xSemaphoreCreateMutex();
    srand((unsigned)esp_timer_get_time());
    esp_task_wdt_init(300, false);
    Serial.printf("Free heap: %u\n", ESP.getFreeHeap());

    // Model header first: its size decides how much cache to allocate.
    g_map = ModelFlash::map();     // nullptr: read from the SD card instead
    g_model_ok = model_init();
    if (!g_model_ok) { _last_error = "bad model.bin"; return false; }

    if (!kv_ram_init()) { _last_error = "KV alloc fail"; g_model_ok = false; return false; }
    Serial.printf("Free heap after KV: %u\n", ESP.getFreeHeap());

    Serial.printf("Free heap before AI task: %u\n", ESP.getFreeHeap());
    // 8KB stack is enough: the engine's big buffers are all global statics
    // (see top of file), not stack locals. The deepest call chain is
    // ai_task -> do_sample(), whose three VOCAB-sized float/int arrays are
    // the biggest stack frame at ~3KB.
    BaseType_t ok = xTaskCreatePinnedToCore(ai_task, "AI_Task", 8192, NULL, 1, &_task_handle, 0);
    if (ok != pdPASS) {
        // Task creation can fail under low RAM (not enough contiguous heap
        // for the stack right after the ~83KB KV cache allocation).
        // g_model_ok must be cleared here so isReady() stops reporting
        // "ready" when nothing can actually process requests.
        Serial.printf("AI task creation FAILED (free heap was %u)\n", ESP.getFreeHeap());
        _last_error = "task fail (low RAM)";
        _task_handle = nullptr;
        kv_ram_free();
        g_model_ok = false;
        return false;
    }

    _last_error = "";
    _started = true;
    return true;
}

inline void deinit() {
    if (_task_handle) {
        // Cooperative stop: ask the task to exit its current generation
        // naturally (so local String/vector objects get destroyed properly
        // when their scope ends), then kill the task. FreeRTOS doesn't
        // unwind the C++ stack on vTaskDelete(), so a hard kill mid-
        // generation would leak any String/vector locals still on the
        // task's stack.
        _stop_requested = true;
        uint32_t t0 = millis();
        while (_busy && millis() - t0 < 10000) { delay(20); }
        _stop_requested = false;
        vTaskDelete(_task_handle);
        _task_handle = nullptr;
    }
    esp_task_wdt_init(5, false);   // back to the system default init() raised
    kv_ram_free();
    ModelFlash::unmap(); g_map = nullptr;
    g_model_ok = false; _started = false;
    _busy = false; _busy_since = 0; _cur_phase = 0; _cur_pos = 0;
}

inline const char* lastError() { return _last_error; }

inline bool   isReady()    { return g_model_ok; }
inline bool   isBusy()     { return _busy; }
inline bool   hasDone()    { return _done; }
inline uint32_t busySince() { return _busy_since; }   // 0 = not busy
inline int    curPos()      { return _cur_pos; }      // 0..KV_MAX_POS-1
inline int    curPhase()    { return _cur_phase; }    // 1=encoding 2=generating

// returns true + fills c if a new streamed token is available
inline bool   pollToken(char &c) {
    if (!_stream_new) return false;
    if (xSemaphoreTake(_mutex, 5)) {
        c = _stream_char;
        _stream_new = false;
        xSemaphoreGive(_mutex);
        return true;
    }
    return false;
}

inline void sendPrompt(const char *p) {
    if (!g_model_ok || _busy) return;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        snprintf(_prompt, sizeof(_prompt), "Q: %s\nA:", p);
        _done = false; _stream_new = false; _request = true;
        xSemaphoreGive(_mutex);
    }
}

inline String getResponse() {
    String r;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        r = String(_response);
        _done = false;
        xSemaphoreGive(_mutex);
    }
    return r;
}

} // namespace MyAI
