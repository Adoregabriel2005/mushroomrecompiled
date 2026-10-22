// 2A03 APU wrapper around Blargg Nes_Snd_Emu 0.1.7 (LGPL, see nes_apu/LGPL.txt).
//
// Linear mixing: a single Blip_Buffer receives all five oscillators with the
// linear per-oscillator gains from Nes_Apu::volume(). Nothing is compressed, so
// each channel's level is independent of what else is playing.
//
// Register writes arrive stamped with the absolute CPU cycle. They are queued
// and forwarded to Nes_Apu with frame-relative times during render() /
// read_status(), so timing does not depend on when audio is drained.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class Apu {
public:
    static constexpr int64_t kCpuHz = 1789773;
    static constexpr int64_t kSampleRate = 48000;

    Apu();
    ~Apu();
    Apu(const Apu&) = delete;
    Apu& operator=(const Apu&) = delete;

    void reset();
    // Select the Blip_Buffer output rate. The default is 48000 Hz.
    // Call between renders; it clears buffered samples.
    void set_sample_rate(int sample_rate);
    int sample_rate() const;
    // Mid-run reset: silence every oscillator now, discard queued writes and any buffered
    // samples, and rebase frame timing to now_cyc so the next render() continues cleanly
    // from there. Unlike reset() it keeps clock rate, sample rate, volume and the DMC
    // reader; call between renders (never from inside one).
    void reset_silence(int64_t now_cyc);
    void write(uint16_t addr, uint8_t val, int64_t cyc);
    // Cycle-stamped like write(): runs the APU forward to cyc before reading.
    uint8_t read_status(int64_t cyc);
    // Advance to until_cyc, appending mono int16 samples to out.
    void render(int64_t until_cyc, std::vector<int16_t>& out);
    // Exact oscillator and frame-sequencer state, kStateBytes bytes appended to out.
    static constexpr size_t kStateBytes = 72;  // sizeof(apu_snapshot_t)
    void save_state(std::vector<uint8_t>& out) const;
    // Restores what save_state wrote and rebases frame timing to now_cyc. Audio still
    // buffered from before the load is dropped.
    void load_state(const uint8_t* p, int64_t now_cyc);
    // Scale the output rate by scale (1.0 = nominal). Used to keep the audio
    // device queue at its target level.
    void set_rate_scale(double scale);

    // CPU memory read used by the DMC to fetch sample bytes. Set by the machine.
    std::function<uint8_t(uint16_t)> read_cpu;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
