#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace murmur {
constexpr float sampleRate = 48000.f;
struct Parameters {
    float bass = 0, mid = 0, treble = 0;
    float bassHz = 180, midHz = 1200, trebleHz = 4500;
    float rate = 48000, bits = 24, pitch = 0, mix = 100, gain = 0;
    bool bypass = false, mute = false;
    float highpass = 20, gate = -80, compression = 0, drive = 0;
    float echo = 0, echoMs = 180;
};
struct Controls {
    std::atomic<float> bass{0}, mid{0}, treble{0};
    std::atomic<float> bassHz{180}, midHz{1200}, trebleHz{4500};
    std::atomic<float> rate{48000}, bits{24}, pitch{0}, mix{100}, gain{0};
    std::atomic<bool> bypass{false}, mute{false};
    std::atomic<float> highpass{20}, gate{-80}, compression{0}, drive{0};
    std::atomic<float> echo{0}, echoMs{180};
    void store(const Parameters&);
    Parameters load() const;
};
class Biquad {
public:
    enum Type { LowShelf, Peak, HighShelf, LowPass, HighPass };
    void configure(Type, double hz, double db, bool immediate = false);
    float tick(float);
    double magnitude(double hz) const;
private:
    std::array<double, 5> c{1,0,0,0,0}, target{1,0,0,0,0}, step{};
    int remaining = 0;
    double z1 = 0, z2 = 0;
};
class Processor {
public:
    void configure(Parameters);
    float tick(float);
    void process(const float* in, float* out, std::size_t count);
    const Parameters& parameters() const { return p; }
    float inputPeak = 0, outputPeak = 0;
    bool limited = false;
private:
    Parameters p{}, previous{};
    bool initialized = false;
    Biquad bass, mid, treble, antiAlias, rumble;
    std::array<float, 8192> delay{};
    // Longest echo is 600 ms at 48 kHz, plus interpolation headroom.
    std::array<float, 28802> echoDelay{};
    std::size_t write = 0;
    std::size_t echoWrite = 0;
    double phase = 0, ratePhase = 1;
    float held = 0, smoothGain = 1, smoothMix = 1, smoothPitchMix = 0;
    float smoothMute = 1, smoothBypass = 0;
    float smoothHighpass = 0, gateEnvelope = 0, gateGain = 1;
    float compressorEnvelope = 0, compressorGain = 1;
    float smoothDrive = 0, smoothEcho = 0, smoothEchoSamples = 8640;
    int gateHold = 0;
    bool gateOpen = true;
    double ratio = 1;
    float readDelay(double samples) const;
};
}
