#pragma once
// M5 speaker sink for ESP8266Audio (triple buffer, no I2S).

#include <Arduino.h>
#include <AudioOutput.h>
#include <M5Cardputer.h>

class AudioOutputM5Speaker : public AudioOutput {
public:
    int MyMusicEqualizer = 0;
    // 0=Flat  1=Warm(low-pass)  2=Bass Boost  3=Treble  4=Voice

    AudioOutputM5Speaker(m5::Speaker_Class *m5sound, uint8_t virtual_sound_channel = 0) {
        _m5sound   = m5sound;
        _virtual_ch = virtual_sound_channel;
    }
    virtual ~AudioOutputM5Speaker() {}
    virtual bool begin() override { return true; }

    virtual bool ConsumeSample(int16_t sample[2]) override {
        int32_t l = sample[0];
        int32_t r = sample[1];

        switch (MyMusicEqualizer) {
        case 1: { // warm low-pass
            const float a = 0.55f;
            l = (int32_t)(lastL + a * (l - lastL));
            r = (int32_t)(lastR + a * (r - lastR));
            lastL = l; lastR = r;
            break;
        }
        case 2: { // Bass Boost
            smoothL = (smoothL * 3 + l) >> 2;
            smoothR = (smoothR * 3 + r) >> 2;
            l = l + (smoothL >> 1);
            r = r + (smoothR >> 1);
            l = l >  32767 ?  32767 : l < -32768 ? -32768 : l;
            r = r >  32767 ?  32767 : r < -32768 ? -32768 : r;
            break;
        }
        case 3: { // Treble boost - high-pass
            int32_t hL = l - lastL, hR = r - lastR;
            lastL = l; lastR = r;
            l = l + (hL >> 1); r = r + (hR >> 1);
            l = l >  32767 ?  32767 : l < -32768 ? -32768 : l;
            r = r >  32767 ?  32767 : r < -32768 ? -32768 : r;
            break;
        }
        case 4: { // Voice clarity - band-pass
            smoothL = (smoothL + l) >> 1;
            smoothR = (smoothR + r) >> 1;
            int32_t bL = l - smoothL + (l - lastL);
            int32_t bR = r - smoothR + (r - lastR);
            lastL = l; lastR = r;
            l = l + (bL >> 2); r = r + (bR >> 2);
            l = l >  32767 ?  32767 : l < -32768 ? -32768 : l;
            r = r >  32767 ?  32767 : r < -32768 ? -32768 : r;
            break;
        }
        default: break; // case 0: Flat
        }

        sample[0] = (int16_t)l;
        sample[1] = (int16_t)r;

        if (_tri_buffer_index < tri_buf_size) {
            _tri_buffer[_tri_index][_tri_buffer_index]     = sample[0];
            _tri_buffer[_tri_index][_tri_buffer_index + 1] = sample[1];
            _tri_buffer_index += 2;
            return true;
        }
        flush();
        return false;
    }

    virtual void flush() override {
        if (_tri_buffer_index) {
            // stereo=true, channel=_virtual_ch
            _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index,
                              hertz, true, 1, _virtual_ch);
            _tri_index = (_tri_index < 2) ? _tri_index + 1 : 0;
            _tri_buffer_index = 0;
        }
    }

    virtual bool stop() override {
        flush();
        _m5sound->stop(_virtual_ch);
        return true;
    }

    const int16_t* getBuffer() const {
        return _tri_buffer[(_tri_index + 2) % 3];
    }

private:
    m5::Speaker_Class* _m5sound;
    uint8_t            _virtual_ch;
    int16_t            lastL = 0, lastR = 0;
    int32_t            smoothL = 0, smoothR = 0;

    static constexpr size_t tri_buf_size = 4096;
    int16_t _tri_buffer[3][tri_buf_size];
    size_t  _tri_buffer_index = 0;
    size_t  _tri_index        = 0;
};
