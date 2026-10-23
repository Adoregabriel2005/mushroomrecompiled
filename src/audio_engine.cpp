#include "audio_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "audio/apu.h"
#include "cyc_core.h"
#include "snd_owner.h"

namespace {

constexpr size_t kApuState = Apu::kStateBytes;
constexpr size_t kOwnerHeader = 4 + 2 + 2 + 4;  // magic, version, header size, rate
constexpr size_t kStateSize = kOwnerHeader + SND_OWNER_STATE_BYTES + kApuState * 2;
constexpr uint16_t kStateVersion = 1;
constexpr char kMagic[4] = {'M', 'R', 'A', 'P'};

struct AudioEngine {
    Apu music;
    Apu sfx;
    std::vector<int16_t> pending;

    AudioEngine() {
        auto fetch = [](uint16_t addr) -> uint8_t {
            uint8_t value = 0x55;
            return cyc_debug_peek(addr, &value) ? value : 0x55;
        };
        music.read_cpu = fetch;
        sfx.read_cpu = fetch;
    }

    void reset(uint64_t cyc, int rate) {
        set_sample_rate(rate);
        music.reset();
        sfx.reset();
        music.read_cpu = [](uint16_t addr) { uint8_t v = 0x55; return cyc_debug_peek(addr, &v) ? v : 0x55; };
        sfx.read_cpu = music.read_cpu;
        snd_owner_reset();
        // The game enables its channels through the music set at startup.
        // The effect set needs the same enables, or a note written before the
        // engine's first $4015 write is silent (as with the music set).
        sfx.write(0x4015, 0x0F, static_cast<int64_t>(cyc));
        pending.clear();
    }

    void set_sample_rate(int rate) {
        if (rate < 8000 || rate > 192000) return;
        music.set_sample_rate(rate);
        sfx.set_sample_rate(rate);
        pending.clear();
    }

    void write(uint16_t addr, uint8_t value, uint64_t cyc) {
        const int64_t now = static_cast<int64_t>(cyc);
        const uint8_t owner = snd_owner_current();
        // The enables register ($4015) is routed by owner, but a write that
        // clears every channel is a "stop everything" gesture: the map's
        // level-entry halt writes $4015=$00 from the sfx side, and if it did
        // not reach both sets the music set would keep its last map note
        // ringing through the whole transition into the level. (The pause
        // writes are wrapped in SND_OWNER_BOTH by codegen; the $0F that
        // usually follows a halt stays per-owner.)
        if (addr == 0x4017 || owner == SND_OWNER_BOTH ||
            (addr == 0x4015 && value == 0x00 && owner == SND_OWNER_SFX)) {
            music.write(addr, value, now);
            sfx.write(addr, value, now);
        } else if (owner == SND_OWNER_SFX) {
            sfx.write(addr, value, now);
        } else {
            music.write(addr, value, now);
        }
    }

    void render(uint64_t cyc) {
        const int64_t now = static_cast<int64_t>(cyc);
        std::vector<int16_t> music_out;
        std::vector<int16_t> sfx_out;
        music.render(now, music_out);
        sfx.render(now, sfx_out);
        const size_t n = std::min(music_out.size(), sfx_out.size());
        pending.reserve(pending.size() + n);
        for (size_t i = 0; i < n; ++i) {
            int32_t sum = static_cast<int32_t>(music_out[i]) + sfx_out[i];
            if (sum > 32767) sum = 32767;
            if (sum < -32768) sum = -32768;
            pending.push_back(static_cast<int16_t>(sum));
        }
    }

    void reset_sfx(uint64_t cyc) {
        const int64_t now = static_cast<int64_t>(cyc);
        sfx.reset_silence(now);
        sfx.write(0x4015, 0x0F, now);
    }

    void save(uint8_t *out) const {
        std::memcpy(out, kMagic, 4);
        uint16_t version = kStateVersion;
        uint16_t header = static_cast<uint16_t>(kOwnerHeader);
        int32_t rate = static_cast<int32_t>(music.sample_rate());
        std::memcpy(out + 4, &version, 2);
        std::memcpy(out + 6, &header, 2);
        std::memcpy(out + 8, &rate, 4);
        snd_owner_save(out + kOwnerHeader);
        std::vector<uint8_t> apu;
        music.save_state(apu);
        sfx.save_state(apu);
        if (apu.size() == kApuState * 2)
            std::memcpy(out + kOwnerHeader + SND_OWNER_STATE_BYTES, apu.data(), apu.size());
    }

    bool validate(const uint8_t *data, size_t len) const {
        if (!data || len != kStateSize) return false;
        if (std::memcmp(data, kMagic, 4) != 0) return false;
        uint16_t version = 0, header = 0;
        int32_t rate = 0;
        std::memcpy(&version, data + 4, 2);
        std::memcpy(&header, data + 6, 2);
        std::memcpy(&rate, data + 8, 4);
        return version == kStateVersion && header == kOwnerHeader &&
               rate >= 8000 && rate <= 192000 &&
               snd_owner_load(data + kOwnerHeader, SND_OWNER_STATE_BYTES, false);
    }

    void load(const uint8_t *data, uint64_t cyc) {
        int32_t rate = 0;
        std::memcpy(&rate, data + 8, 4);
        set_sample_rate(rate);
        snd_owner_load(data + kOwnerHeader, SND_OWNER_STATE_BYTES, true);
        const uint8_t *apu = data + kOwnerHeader + SND_OWNER_STATE_BYTES;
        music.load_state(apu, static_cast<int64_t>(cyc));
        sfx.load_state(apu + kApuState, static_cast<int64_t>(cyc));
        pending.clear();
    }
};

AudioEngine &engine() {
    static AudioEngine instance;
    return instance;
}

}  // namespace

void cyc_audio_power_on(uint64_t cyc, int sample_rate) {
    engine().reset(cyc, sample_rate);
}

void cyc_audio_set_sample_rate(int sample_rate) {
    engine().set_sample_rate(sample_rate);
}

void cyc_audio_write(uint16_t addr, uint8_t value, uint64_t cyc) {
    engine().write(addr, value, cyc);
}

void cyc_audio_reset_sfx(void) {
    engine().reset_sfx(cyc_cycle_count());
}

void cyc_audio_set_rate_scale(double scale) {
    if (!std::isfinite(scale)) return;
    scale = std::clamp(scale, 0.5, 2.0);
    engine().music.set_rate_scale(scale);
    engine().sfx.set_rate_scale(scale);
}

size_t cyc_audio_read_samples(int16_t *out, size_t max) {
    if (!out || max == 0) return 0;
    AudioEngine &e = engine();
    e.render(cyc_cycle_count());
    const size_t n = std::min(max, e.pending.size());
    if (n) {
        std::memcpy(out, e.pending.data(), n * sizeof(*out));
        e.pending.erase(e.pending.begin(), e.pending.begin() + static_cast<std::ptrdiff_t>(n));
    }
    return n;
}

void cyc_audio_render(uint64_t cyc) {
    engine().render(cyc);
}

size_t cyc_audio_state_size(void) {
    return kStateSize;
}

void cyc_audio_state_save(uint8_t *out) {
    if (out) engine().save(out);
}

bool cyc_audio_state_validate(const uint8_t *data, size_t len) {
    return engine().validate(data, len);
}

void cyc_audio_state_load(const uint8_t *data, uint64_t cyc) {
    if (data) engine().load(data, cyc);
}
