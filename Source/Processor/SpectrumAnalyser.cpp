#include "Processor/SpectrumAnalyser.h"

#include <cmath>
#include <cstring>
#include <memory>

namespace eqm1 {

namespace {
constexpr int kNumFrames = 8;
}

//------------------------------------------------------------------------------
SpectrumAnalyser::SpectrumAnalyser(int fftOrder)
    : fftSize(1 << fftOrder),
      hopSize(fftSize / 4),
      numMagnitudes(fftSize / 2 + 1),
      fft(fftOrder),
      fifo(kNumFrames) {
    window.resize(static_cast<size_t>(fftSize));
    ring.assign(static_cast<size_t>(fftSize), 0.0f);
    workspace.assign(static_cast<size_t>(fftSize) * 2, 0.0f);
    frames.resize(static_cast<size_t>(kNumFrames));

    for (int i = 0; i < fftSize; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(fftSize - 1);
        window[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * t));
    }
}

void SpectrumAnalyser::prepare(double sampleRate) {
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void SpectrumAnalyser::reset() {
    std::fill(ring.begin(), ring.end(), 0.0f);
    writePos = 0;
    sinceHop = 0;
    fifo.reset();
}

void SpectrumAnalyser::publishFrame(const double* dbValues) {
    if (fifo.getFreeSpace() <= 0) {
        int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
        fifo.prepareToRead(1, s1, n1, s2, n2);
        fifo.finishedRead(n1 + n2);
    }

    Frame frame{};
    for (int i = 0; i < kSpectrumBins; ++i)
        frame[static_cast<size_t>(i)] = static_cast<float>(dbValues[i]);

    int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
    fifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0)
        frames[static_cast<size_t>(s1)] = frame;
    fifo.finishedWrite(n1 + n2);
}

void SpectrumAnalyser::analyseWindow() {
    if (fs <= 0.0)
        return;

    for (int i = 0; i < fftSize; ++i) {
        const size_t src = static_cast<size_t>((writePos + i) % fftSize);
        workspace[static_cast<size_t>(i)] = ring[src] * window[static_cast<size_t>(i)];
    }
    std::fill(workspace.begin() + fftSize, workspace.end(), 0.0f);

    fft.performFrequencyOnlyForwardTransform(workspace.data(), true);

    std::array<double, kSpectrumBins> db{};
    binSpectrumToLogBands(workspace.data(), numMagnitudes, fs, db.data(), kSpectrumBins,
                          kSpectrumMinFreq, kSpectrumMaxFreq);
    publishFrame(db.data());
}

template <typename SampleType>
void SpectrumAnalyser::pushBlockImpl(const SampleType* const* channels, int numChannels,
                                     int numSamples) {
    if (!active.load(std::memory_order_relaxed) || fs <= 0.0 || channels == nullptr
        || numSamples <= 0)
        return;

    const int used = std::min(numChannels, kNumChannels);
    const double inv = 1.0 / static_cast<double>(std::max(used, 1));

    for (int i = 0; i < numSamples; ++i) {
        double sum = 0.0;
        for (int c = 0; c < used; ++c) {
            if (channels[c] != nullptr)
                sum += static_cast<double>(channels[c][i]);
        }

        ring[static_cast<size_t>(writePos)] = static_cast<float>(sum * inv);
        writePos = (writePos + 1) % fftSize;

        if (++sinceHop >= hopSize) {
            sinceHop = 0;
            analyseWindow();
        }
    }
}

void SpectrumAnalyser::pushBlock(const float* const* channels, int numChannels, int numSamples) {
    pushBlockImpl(channels, numChannels, numSamples);
}

void SpectrumAnalyser::pushBlock(const double* const* channels, int numChannels, int numSamples) {
    pushBlockImpl(channels, numChannels, numSamples);
}

bool SpectrumAnalyser::readLatestFrame(Frame& out) {
    const int ready = fifo.getNumReady();
    if (ready <= 0)
        return false;

    int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
    fifo.prepareToRead(ready, s1, n1, s2, n2);

    for (int i = 0; i < n1; ++i)
        out = frames[static_cast<size_t>(s1 + i)];
    for (int i = 0; i < n2; ++i)
        out = frames[static_cast<size_t>(s2 + i)];

    fifo.finishedRead(n1 + n2);
    return n1 + n2 > 0;
}

//------------------------------------------------------------------------------
Decimator4x::Decimator4x() {
    // Windowed sinc at fs/8 of the input rate: enough stopband for a display analyser.
    const double wc = kPi / 4.0;
    double sum = 0.0;
    for (int t = 0; t < kTaps; ++t) {
        const double n = static_cast<double>(t) - (kTaps - 1) / 2.0;
        const double sinc = std::abs(n) < 1.0e-9 ? wc / kPi : std::sin(wc * n) / (kPi * n);
        const double win = 0.5 - 0.5 * std::cos(2.0 * kPi * t / (kTaps - 1));
        coef[static_cast<size_t>(t)] = static_cast<float>(sinc * win);
        sum += coef[static_cast<size_t>(t)];
    }
    for (auto& c : coef)
        c = static_cast<float>(c / sum);
}

void Decimator4x::reset() {
    for (auto& h : history)
        h.fill(0.0f);
    count.fill(0);
}

void Decimator4x::process(const float* inL, const float* inR, float* outL, float* outR,
                          int numSamples) {
    const float* in[2] = {inL, inR};
    float* out[2] = {outL, outR};

    for (int c = 0; c < 2; ++c) {
        if (in[c] == nullptr || out[c] == nullptr)
            continue;

        auto& h = history[static_cast<size_t>(c)];
        int& cnt = count[static_cast<size_t>(c)];
        int o = 0;

        for (int i = 0; i < numSamples; ++i) {
            std::memmove(h.data() + 1, h.data(), sizeof(float) * (kTaps - 1));
            h[0] = in[c][i];

            if ((cnt & 3) == 3) {
                float acc = 0.0f;
                for (int t = 0; t < kTaps; ++t)
                    acc += coef[static_cast<size_t>(t)] * h[static_cast<size_t>(t)];
                out[c][o++] = acc;
            }
            ++cnt;
        }
    }
}

//------------------------------------------------------------------------------
MultiResAnalyser::MultiResAnalyser() {
    stages[0] = std::make_unique<SpectrumAnalyser>(12);   // 4096 @ fs
    stages[1] = std::make_unique<SpectrumAnalyser>(11);   // 2048 @ fs/4
    stages[2] = std::make_unique<SpectrumAnalyser>(10);   // 1024 @ fs/16
    stages[3] = std::make_unique<SpectrumAnalyser>(9);    //  512 @ fs/64
    for (auto& f : lastFrames)
        f.fill(static_cast<float>(kSpectrumFloorDb));
}

void MultiResAnalyser::prepare(double sampleRate, int maxBlockSize) {
    sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

    int cap = std::max(64, maxBlockSize);
    for (int k = 0; k < kStages; ++k) {
        work[static_cast<size_t>(k)].assign(static_cast<size_t>(2 * cap + 16), 0.0f);
        stages[static_cast<size_t>(k)]->prepare(sampleRate / std::pow(4.0, k));
        cap = std::max(16, cap / 4);
    }

    for (auto& d : decimators)
        d.reset();

    haveFrame.fill(false);
    for (auto& f : lastFrames)
        f.fill(static_cast<float>(kSpectrumFloorDb));
}

void MultiResAnalyser::reset() {
    for (auto& s : stages)
        s->reset();
    for (auto& d : decimators)
        d.reset();
    haveFrame.fill(false);
}

void MultiResAnalyser::setActive(bool shouldBeActive) {
    for (auto& s : stages)
        s->setActive(shouldBeActive);
}

template <typename SampleType>
void MultiResAnalyser::pushBlockImpl(const SampleType* const* channels, int numChannels,
                                     int numSamples) {
    if (channels == nullptr || numSamples <= 0 || numChannels <= 0)
        return;
    if (static_cast<size_t>(2 * numSamples) > work[0].size())
        return;   // block larger than prepared for: ignore rather than overrun

    float* l = work[0].data();
    float* r = l + numSamples;

    const bool hasRight = numChannels > 1 && channels[1] != nullptr;
    for (int i = 0; i < numSamples; ++i) {
        l[i] = static_cast<float>(channels[0] != nullptr ? channels[0][i] : SampleType(0));
        r[i] = static_cast<float>(hasRight ? channels[1][i] : l[i]);
    }

    const float* inL = l;
    const float* inR = r;
    int len = numSamples;

    {
        const float* ptrs[2] = {inL, inR};
        stages[0]->pushBlock(ptrs, 2, len);
    }

    for (int k = 1; k < kStages; ++k) {
        const int outLen = len / 4;
        if (outLen <= 0)
            break;

        float* wl = work[static_cast<size_t>(k)].data();
        float* wr = wl + outLen + 4;
        decimators[static_cast<size_t>(k - 1)].process(inL, inR, wl, wr, len);

        inL = wl;
        inR = wr;
        len = outLen;

        const float* ptrs[2] = {inL, inR};
        stages[static_cast<size_t>(k)]->pushBlock(ptrs, 2, len);
    }
}

void MultiResAnalyser::pushBlock(const float* const* channels, int numChannels, int numSamples) {
    pushBlockImpl(channels, numChannels, numSamples);
}

void MultiResAnalyser::pushBlock(const double* const* channels, int numChannels, int numSamples) {
    pushBlockImpl(channels, numChannels, numSamples);
}

void MultiResAnalyser::mergeFrames(Frame& out) const {
    // Boundary between stage k and the next coarser one, blended over a quarter octave
    // either side so the hand-over is invisible.
    const double crossovers[kStages - 1] = {1450.0, 360.0, 90.0};

    static int dbgCount = 0;
    const bool dbg = dbgCount < 3;
    if (dbg)
        ++dbgCount;

    for (int i = 0; i < kSpectrumBins; ++i) {
        const double f = frequencyForDisplayBin(i, kSpectrumBins, kSpectrumMinFreq, kSpectrumMaxFreq);

        double v = lastFrames[kStages - 1][static_cast<size_t>(i)];
        for (int k = kStages - 2; k >= 0; --k) {
            const double c = crossovers[static_cast<size_t>(k)];
            const double lo = std::log(c / 1.25);
            const double hi = std::log(c * 1.25);
            double t = (std::log(f) - lo) / (hi - lo);
            t = juce::jlimit(0.0, 1.0, t);
            const double w = t * t * (3.0 - 2.0 * t);
            v += (lastFrames[static_cast<size_t>(k)][static_cast<size_t>(i)] - v) * w;
        }

        out[static_cast<size_t>(i)] = static_cast<float>(v);

        if (dbg && f > 950.0 && f < 1050.0)
            std::printf("[dbg] f=%.0f s0=%.1f s1=%.1f s2=%.1f s3=%.1f merged=%.1f\n", f,
                        lastFrames[0][static_cast<size_t>(i)], lastFrames[1][static_cast<size_t>(i)],
                        lastFrames[2][static_cast<size_t>(i)], lastFrames[3][static_cast<size_t>(i)], v);
    }
}

bool MultiResAnalyser::readLatestFrame(Frame& out) {
    bool any = false;

    for (int k = 0; k < kStages; ++k) {
        Frame f;
        if (stages[static_cast<size_t>(k)]->readLatestFrame(f)) {
            lastFrames[static_cast<size_t>(k)] = f;
            haveFrame[static_cast<size_t>(k)] = true;
            any = true;
        }
    }

    if (!haveFrame[0])
        return false;

    mergeFrames(out);
    return any;
}

}  // namespace eqm1
