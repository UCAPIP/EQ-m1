#pragma once

#include <array>

#include "DSP/Biquad.h"

namespace eqm1 {

inline constexpr int kSpectrumBins = 600;
inline constexpr double kSpectrumMinFreq = 20.0;
inline constexpr double kSpectrumMaxFreq = 20000.0;
inline constexpr double kSpectrumFloorDb = -144.0;

// Cascade magnitude response built from parameter values only: no shared mutable
// state, so the GUI thread and the audio thread can each own an instance.
class MagnitudeCurve {
public:
    void update(const BandParams* bands, int numBands, double sampleRate);

    double at(double freqHz) const;

    // Writes numPoints dB values at log-spaced frequencies spanning [fMin, fMax].
    void render(double* outDb, int numPoints, double fMin, double fMax) const;

    // Broadband level compensation: minus the mean of the curve over a
    // pink-weighted (log-uniform) grid.
    double autoGainDb(int numPoints = 96) const;

    bool isEmpty() const noexcept { return empty; }
    double sampleRate() const noexcept { return fs; }
    int numBands() const noexcept { return count; }
    bool bandActive(int i) const noexcept { return i >= 0 && i < count && active[static_cast<size_t>(i)]; }
    const CoefficientSet& bandSet(int i) const noexcept { return sets[static_cast<size_t>(i)]; }

private:
    std::array<CoefficientSet, kMaxBands> sets{};
    std::array<bool, kMaxBands> active{};
    int count = 0;
    double fs = 48000.0;
    bool empty = true;
};

// Maps a linear FFT magnitude spectrum onto log-spaced display bands, in dB.
// A full-scale sine should read close to 0 dB.
void binSpectrumToLogBands(const float* magnitudes, int numBins, double sampleRate,
                           double* outDb, int numOutBins, double fMin, double fMax);

// Centre frequency of display bin `binIndex` out of `numBins`.
double frequencyForDisplayBin(int binIndex, int numBins, double fMin, double fMax);

}  // namespace eqm1
