#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>

#include "DSP/Biquad.h"
#include "DSP/Curve.h"

namespace eqm1 {

// One-pole glide towards a target, advanced once per sub-block.
class ParamSmoother {
public:
    ParamSmoother(double initialValue = 0.0, double timeConstant = 0.012,
                  double epsilon = 1.0e-6) noexcept
        : value(initialValue), target(initialValue), tc(timeConstant), eps(epsilon) {}

    void setSampleRate(double sampleRate, int subBlock) noexcept {
        rate = sampleRate / static_cast<double>(std::max(1, subBlock));
        updateCoeff();
    }

    void setTimeConstant(double seconds) noexcept {
        tc = std::max(seconds, 1.0e-4);
        updateCoeff();
    }

    void setEpsilon(double e) noexcept { eps = e; }
    void setTarget(double t) noexcept { target = t; }
    void snap(double t) noexcept { target = t; value = t; }
    void snapToTarget() noexcept { value = target; }
    double get() const noexcept { return value; }

    // Returns true while the value is still travelling towards the target.
    bool advance() noexcept {
        const double d = target - value;
        if (std::abs(d) <= eps) {
            value = target;
            return false;
        }
        value += coeff * d;
        return true;
    }

private:
    void updateCoeff() noexcept {
        coeff = 1.0 - std::exp(-1.0 / std::max(tc * rate, 1.0e-6));
    }

    double value, target, tc, eps;
    double rate = 1000.0;
    double coeff = 0.05;
};

// The equaliser: a cascade of kMaxBands bands, each with its own channel mode,
// plus a click-free global bypass, per-band enable crossfades, auto gain and a
// solo monitoring path that isolates the frequency region around a band.
class EqEngine {
public:
    static constexpr int kSubBlock = 32;

    EqEngine() = default;

    void prepare(double sampleRate);
    void reset();

    // Audio thread only: hand over the current parameter snapshot for this block.
    void setParameters(const EngineInput& input);

    // Stereo only (2 channels), numSamples may be any length.
    template <typename SampleType>
    void process(const SampleType* const* inChannels, SampleType* const* outChannels, int numSamples);

    double currentAutoGainDb() const noexcept { return gainToDb(autoGainSm.get()); }
    bool isSoloActive() const noexcept { return soloMixSm.get() > 1.0e-4 || anySolo; }
    double sampleRate() const noexcept { return fs; }
    bool isPrepared() const noexcept { return prepared; }
    const EngineInput& parameters() const noexcept { return current; }

private:
    using StateArray = BiquadState[kNumChannels][kMaxSections];

    struct BandRuntime {
        BandParams smoothed;   // gliding frequency/gain/q + current discrete settings
        BandParams built;      // snapshot the cached coefficients were built from
        CoefficientSet coeffs;
        CoefficientSet soloCoeffs;   // same design as coeffs, independent state
        CoefficientSet soloBp;       // isolation band-pass (kSoloSections)
        StateArray state{};
        StateArray soloState{};
        StateArray soloBpState{};
        ParamSmoother logFreqSm{std::log(1000.0)};
        ParamSmoother gainSm{0.0};
        ParamSmoother qSm{0.707};
        ParamSmoother enableSm{0.0, 0.02, 1.0e-5};
        double enableMix = 0.0;
        bool moving = true;
        bool builtValid = false;
        bool wasSoloed = false;
    };

    template <typename SampleType>
    void processSubBlock(const SampleType* inL, const SampleType* inR,
                         SampleType* outL, SampleType* outR, int n);

    void updateSmoothers();
    void snapAll();
    void rebuildCoefficients();
    void refreshAutoGain();
    void applyMainChain(double* L, double* R, int n);
    void applyBand(BandRuntime& b, double* L, double* R, int n);
    void accumulateSoloBand(int bandIndex, const double* dryL, const double* dryR,
                            double* accL, double* accR, int n);
    void filterChannelMode(ChannelMode mode, const CoefficientSet& set, StateArray& state,
                           double* L, double* R, int n);
    static void clearStates(StateArray& state) noexcept;

    BandRuntime bands[kMaxBands];
    EngineInput current;
    MagnitudeCurve autoGainCurve;

    ParamSmoother outGainSm{1.0, 0.02, 1.0e-6};
    ParamSmoother autoGainSm{1.0, 0.05, 1.0e-6};
    ParamSmoother soloMixSm{0.0, 0.03, 1.0e-5};
    ParamSmoother bypassMixSm{0.0, 0.02, 1.0e-5};
    ParamSmoother soloWidthSm{1.0, 0.05, 1.0e-5};

    double fs = 48000.0;
    bool prepared = false;
    bool anySolo = false;
    bool soloWidthMoving = false;
    double autoGainTargetLin = 1.0;
};

template <typename SampleType>
void EqEngine::process(const SampleType* const* inChannels, SampleType* const* outChannels,
                       int numSamples) {
    if (numSamples <= 0 || inChannels == nullptr || outChannels == nullptr)
        return;
    if (inChannels[0] == nullptr || inChannels[1] == nullptr)
        return;

    for (int offset = 0; offset < numSamples; offset += kSubBlock) {
        const int n = std::min(kSubBlock, numSamples - offset);
        processSubBlock(inChannels[0] + offset, inChannels[1] + offset,
                        outChannels[0] + offset, outChannels[1] + offset, n);
    }
}

template <typename SampleType>
void EqEngine::processSubBlock(const SampleType* inL, const SampleType* inR,
                               SampleType* outL, SampleType* outR, int n) {
    updateSmoothers();
    rebuildCoefficients();

    double dryL[kSubBlock], dryR[kSubBlock];
    double wetL[kSubBlock], wetR[kSubBlock];

    for (int i = 0; i < n; ++i) {
        dryL[i] = static_cast<double>(inL[i]);
        dryR[i] = static_cast<double>(inR[i]);
        wetL[i] = dryL[i];
        wetR[i] = dryR[i];
    }

    const double bypassMix = bypassMixSm.get();
    const double soloMix = soloMixSm.get();
    const bool wantMain = bypassMix < 0.9999 && soloMix < 0.9999;
    const bool wantSolo = bypassMix < 0.9999 && (anySolo || soloMix > 1.0e-4);

    if (wantMain) {
        applyMainChain(wetL, wetR, n);
    } else if (wantSolo) {
        std::memset(wetL, 0, sizeof(double) * static_cast<size_t>(n));
        std::memset(wetR, 0, sizeof(double) * static_cast<size_t>(n));
    }

    if (wantSolo) {
        double sL[kSubBlock] = {0};
        double sR[kSubBlock] = {0};
        for (int i = 0; i < kMaxBands; ++i) {
            if (current.bands[i].solo)
                accumulateSoloBand(i, dryL, dryR, sL, sR, n);
        }
        const double keep = 1.0 - soloMix;
        for (int i = 0; i < n; ++i) {
            wetL[i] = wetL[i] * keep + sL[i] * soloMix;
            wetR[i] = wetR[i] * keep + sR[i] * soloMix;
        }
    }

    if (bypassMix < 0.9999) {
        // Auto gain fades out as solo engages, so that solo monitoring reports the
        // level the band actually applies.
        const double autoAmount = 1.0 + (autoGainSm.get() - 1.0) * (1.0 - soloMix);
        const double g = outGainSm.get() * autoAmount;
        if (std::abs(g - 1.0) > 1.0e-9) {
            for (int i = 0; i < n; ++i) {
                wetL[i] *= g;
                wetR[i] *= g;
            }
        }
    }

    if (bypassMix > 1.0e-4) {
        const double keep = 1.0 - bypassMix;
        for (int i = 0; i < n; ++i) {
            wetL[i] = dryL[i] * bypassMix + wetL[i] * keep;
            wetR[i] = dryR[i] * bypassMix + wetR[i] * keep;
        }
    }

    for (int i = 0; i < n; ++i) {
        outL[i] = static_cast<SampleType>(wetL[i]);
        outR[i] = static_cast<SampleType>(wetR[i]);
    }
}

}  // namespace eqm1
