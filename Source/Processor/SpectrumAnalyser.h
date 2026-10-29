#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <vector>

#include "DSP/Curve.h"

namespace eqm1 {

// Log-binned spectrum source for a single analysis window size. The audio thread
// produces frames, the GUI thread consumes them through a lock-free SPSC FIFO.
class SpectrumAnalyser {
public:
    using Frame = std::array<float, kSpectrumBins>;

    explicit SpectrumAnalyser(int fftOrder = 12);

    void prepare(double sampleRate);
    void reset();

    void pushBlock(const float* const* channels, int numChannels, int numSamples);
    void pushBlock(const double* const* channels, int numChannels, int numSamples);

    // GUI thread: drains the FIFO and keeps the newest frame.
    bool readLatestFrame(Frame& out);

    double getSampleRate() const noexcept { return fs; }
    void setActive(bool shouldBeActive) noexcept { active.store(shouldBeActive); }
    bool isActive() const noexcept { return active.load(); }

private:
    template <typename SampleType>
    void pushBlockImpl(const SampleType* const* channels, int numChannels, int numSamples);

    void analyseWindow();
    void publishFrame(const double* dbValues);

    const int fftSize;
    const int hopSize;
    const int numMagnitudes;

    juce::dsp::FFT fft;
    std::vector<float> window;
    std::vector<float> ring;
    std::vector<float> workspace;
    std::vector<Frame> frames;
    juce::AbstractFifo fifo;

    int writePos = 0;
    int sinceHop = 0;
    double fs = 48000.0;
    std::atomic<bool> active{true};
};

// 4x FIR decimator (48-tap windowed sinc, cutoff fs/8, ~-40 dB stopband) used to build
// the low-frequency stages of the multi-resolution analyser.
class Decimator4x {
public:
    Decimator4x();

    void reset();
    // Writes numSamples/4 samples per channel.
    void process(const float* inL, const float* inR, float* outL, float* outR, int numSamples);

private:
    static constexpr int kTaps = 48;
    std::array<float, kTaps> coef{};
    std::array<std::array<float, kTaps>, 2> history{};
    std::array<int, 2> count{};
};

// Pro-Q style analyser: four decimated stages (x1, x4, x16, x64) merged into one
// log-spaced frame, so the low end keeps the same visual resolution as the top end.
class MultiResAnalyser {
public:
    using Frame = SpectrumAnalyser::Frame;

    MultiResAnalyser();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void pushBlock(const float* const* channels, int numChannels, int numSamples);
    void pushBlock(const double* const* channels, int numChannels, int numSamples);

    bool readLatestFrame(Frame& out);

    void setActive(bool shouldBeActive);
    bool isActive() const noexcept { return stages[0]->isActive(); }
    double getSampleRate() const noexcept { return sampleRate; }

private:
    template <typename SampleType>
    void pushBlockImpl(const SampleType* const* channels, int numChannels, int numSamples);

    void mergeFrames(Frame& out) const;

    static constexpr int kStages = 4;

    std::array<std::unique_ptr<SpectrumAnalyser>, kStages> stages;
    std::array<Decimator4x, kStages - 1> decimators;
    std::array<std::vector<float>, kStages> work;
    mutable std::array<Frame, kStages> lastFrames;
    mutable std::array<bool, kStages> haveFrame{};

    double sampleRate = 48000.0;
};

}  // namespace eqm1
