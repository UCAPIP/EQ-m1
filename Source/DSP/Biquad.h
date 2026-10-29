#pragma once

#include "DSP/DspTypes.h"

namespace eqm1 {

// Normalised biquad coefficients (a0 == 1).
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static BiquadCoeffs identity() noexcept { return {}; }

    bool isIdentity() const noexcept {
        return b0 == 1.0 && b1 == 0.0 && b2 == 0.0 && a1 == 0.0 && a2 == 0.0;
    }
};

// Transposed direct form II state. Kept in double regardless of the sample type.
struct BiquadState {
    double z1 = 0.0, z2 = 0.0;
    void reset() noexcept { z1 = 0.0; z2 = 0.0; }
};

template <typename SampleType>
inline SampleType processBiquad(const BiquadCoeffs& c, BiquadState& s, SampleType x) noexcept {
    const double xd = static_cast<double>(x);
    const double y = c.b0 * xd + s.z1;
    s.z1 = c.b1 * xd - c.a1 * y + s.z2;
    s.z2 = c.b2 * xd - c.a2 * y;
    return static_cast<SampleType>(y);
}

// A band is realised as a cascade of 1..kMaxSections biquads.
struct CoefficientSet {
    BiquadCoeffs sections[kMaxSections];
    int numSections = 0;

    void clear() noexcept { numSections = 0; }
    void push(const BiquadCoeffs& c) noexcept;
};

template <typename SampleType>
inline void processSet(const CoefficientSet& set, BiquadState* state, SampleType& x) noexcept {
    for (int i = 0; i < set.numSections; ++i)
        x = processBiquad(set.sections[i], state[i], x);
}

template <typename SampleType>
inline SampleType runSet(const CoefficientSet& set, BiquadState* state, SampleType x) noexcept {
    for (int i = 0; i < set.numSections; ++i)
        x = processBiquad(set.sections[i], state[i], x);
    return x;
}

// --- designs (RBJ Audio-EQ-Cookbook, bilinear transform with pre-warping) ---
BiquadCoeffs makeBell(double f, double fs, double gainDb, double q);
BiquadCoeffs makeLowShelf(double f, double fs, double gainDb, double q);
BiquadCoeffs makeHighShelf(double f, double fs, double gainDb, double q);
BiquadCoeffs makeNotch(double f, double fs, double q);
BiquadCoeffs makeBandPass(double f, double fs, double q);   // 0 dB peak gain
BiquadCoeffs makeAllPass(double f, double fs, double q);
BiquadCoeffs makeLowPassSection(double f, double fs, double q);
BiquadCoeffs makeHighPassSection(double f, double fs, double q);
BiquadCoeffs makeFirstOrderLowPass(double f, double fs);
BiquadCoeffs makeFirstOrderHighPass(double f, double fs);

// Q of the k-th second order section of an N-th order Butterworth filter, k in [1, N/2].
double butterworthQ(int order, int sectionIndex);
void makeButterworthLowPass(double f, double fs, int order, CoefficientSet& out);
void makeButterworthHighPass(double f, double fs, int order, CoefficientSet& out);

CoefficientSet buildBandCoefficients(const BandParams& p, double sampleRate);

// --- magnitude response ---------------------------------------------------
// cosW/sinW/cos2W/sin2W are precomputed once per frequency so that a whole
// cascade can be swept cheaply.
inline double biquadMagnitude(const BiquadCoeffs& c, double cosW, double sinW,
                              double cos2W, double sin2W) noexcept {
    const double nr = c.b0 + c.b1 * cosW + c.b2 * cos2W;
    const double ni = -(c.b1 * sinW + c.b2 * sin2W);
    const double dr = 1.0 + c.a1 * cosW + c.a2 * cos2W;
    const double di = -(c.a1 * sinW + c.a2 * sin2W);
    return std::sqrt(nr * nr + ni * ni) / std::max(std::sqrt(dr * dr + di * di), 1.0e-12);
}

double setMagnitude(const CoefficientSet& set, double cosW, double sinW,
                    double cos2W, double sin2W) noexcept;
double setMagnitudeDb(const CoefficientSet& set, double freqHz, double sampleRate) noexcept;
double bandMagnitudeDb(const BandParams& p, double freqHz, double sampleRate);

}  // namespace eqm1
