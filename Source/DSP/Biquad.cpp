#include "DSP/Biquad.h"

namespace eqm1 {

namespace {

struct Warped {
    double cw = 1.0, sw = 0.0, alpha = 0.0;
};

Warped prepare(double f, double fs, double q) {
    const double freq = clampFreq(f, fs);
    const double w0 = 2.0 * kPi * freq / fs;
    Warped w;
    w.cw = std::cos(w0);
    w.sw = std::sin(w0);
    w.alpha = w.sw / (2.0 * std::clamp(q, 1.0e-4, 1.0e4));
    return w;
}

}  // namespace

void CoefficientSet::push(const BiquadCoeffs& c) noexcept {
    if (numSections < kMaxSections)
        sections[numSections++] = c;
}

BiquadCoeffs makeBell(double f, double fs, double gainDb, double q) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const Warped w = prepare(f, fs, q);
    const double a0 = 1.0 + w.alpha / A;
    const double inv = 1.0 / a0;

    BiquadCoeffs c;
    c.b0 = (1.0 + w.alpha * A) * inv;
    c.b1 = -2.0 * w.cw * inv;
    c.b2 = (1.0 - w.alpha * A) * inv;
    c.a1 = -2.0 * w.cw * inv;
    c.a2 = (1.0 - w.alpha / A) * inv;
    return c;
}

BiquadCoeffs makeLowShelf(double f, double fs, double gainDb, double q) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const Warped w = prepare(f, fs, q);
    const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * w.alpha;
    const double a0 = (A + 1.0) + (A - 1.0) * w.cw + twoSqrtAAlpha;
    const double inv = 1.0 / a0;

    BiquadCoeffs c;
    c.b0 = A * ((A + 1.0) - (A - 1.0) * w.cw + twoSqrtAAlpha) * inv;
    c.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * w.cw) * inv;
    c.b2 = A * ((A + 1.0) - (A - 1.0) * w.cw - twoSqrtAAlpha) * inv;
    c.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * w.cw) * inv;
    c.a2 = ((A + 1.0) + (A - 1.0) * w.cw - twoSqrtAAlpha) * inv;
    return c;
}

BiquadCoeffs makeHighShelf(double f, double fs, double gainDb, double q) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const Warped w = prepare(f, fs, q);
    const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * w.alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * w.cw + twoSqrtAAlpha;
    const double inv = 1.0 / a0;

    BiquadCoeffs c;
    c.b0 = A * ((A + 1.0) + (A - 1.0) * w.cw + twoSqrtAAlpha) * inv;
    c.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * w.cw) * inv;
    c.b2 = A * ((A + 1.0) + (A - 1.0) * w.cw - twoSqrtAAlpha) * inv;
    c.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * w.cw) * inv;
    c.a2 = ((A + 1.0) - (A - 1.0) * w.cw - twoSqrtAAlpha) * inv;
    return c;
}

BiquadCoeffs makeNotch(double f, double fs, double q) {
    const Warped w = prepare(f, fs, q);
    const double a0 = 1.0 + w.alpha;
    const double inv = 1.0 / a0;

    BiquadCoeffs c;
    c.b0 = 1.0 * inv;
    c.b1 = -2.0 * w.cw * inv;
    c.b2 = 1.0 * inv;
    c.a1 = -2.0 * w.cw * inv;
    c.a2 = (1.0 - w.alpha) * inv;
    return c;
}

BiquadCoeffs makeBandPass(double f, double fs, double q) {
    const Warped w = prepare(f, fs, q);
    const double a0 = 1.0 + w.alpha;
    const double inv = 1.0 / a0;

    BiquadCoeffs c;
    c.b0 = w.alpha * inv;
    c.b1 = 0.0;
    c.b2 = -w.alpha * inv;
    c.a1 = -2.0 * w.cw * inv;
    c.a2 = (1.0 - w.alpha) * inv;
    return c;
}

BiquadCoeffs makeAllPass(double f, double fs, double q) {
    const Warped w = prepare(f, fs, q);
    const double a0 = 1.0 + w.alpha;
    const double inv = 1.0 / a0;

    BiquadCoeffs c;
    c.b0 = (1.0 - w.alpha) * inv;
    c.b1 = -2.0 * w.cw * inv;
    c.b2 = (1.0 + w.alpha) * inv;
    c.a1 = -2.0 * w.cw * inv;
    c.a2 = (1.0 - w.alpha) * inv;
    return c;
}

BiquadCoeffs makeLowPassSection(double f, double fs, double q) {
    const Warped w = prepare(f, fs, q);
    const double a0 = 1.0 + w.alpha;
    const double inv = 1.0 / a0;

    BiquadCoeffs c;
    c.b0 = 0.5 * (1.0 - w.cw) * inv;
    c.b1 = (1.0 - w.cw) * inv;
    c.b2 = 0.5 * (1.0 - w.cw) * inv;
    c.a1 = -2.0 * w.cw * inv;
    c.a2 = (1.0 - w.alpha) * inv;
    return c;
}

BiquadCoeffs makeHighPassSection(double f, double fs, double q) {
    const Warped w = prepare(f, fs, q);
    const double a0 = 1.0 + w.alpha;
    const double inv = 1.0 / a0;

    BiquadCoeffs c;
    c.b0 = 0.5 * (1.0 + w.cw) * inv;
    c.b1 = -(1.0 + w.cw) * inv;
    c.b2 = 0.5 * (1.0 + w.cw) * inv;
    c.a1 = -2.0 * w.cw * inv;
    c.a2 = (1.0 - w.alpha) * inv;
    return c;
}

BiquadCoeffs makeFirstOrderLowPass(double f, double fs) {
    const double g = std::tan(kPi * clampFreq(f, fs) / fs);
    const double inv = 1.0 / (1.0 + g);

    BiquadCoeffs c;
    c.b0 = g * inv;
    c.b1 = g * inv;
    c.b2 = 0.0;
    c.a1 = (g - 1.0) * inv;
    c.a2 = 0.0;
    return c;
}

BiquadCoeffs makeFirstOrderHighPass(double f, double fs) {
    const double g = std::tan(kPi * clampFreq(f, fs) / fs);
    const double inv = 1.0 / (1.0 + g);

    BiquadCoeffs c;
    c.b0 = 1.0 * inv;
    c.b1 = -1.0 * inv;
    c.b2 = 0.0;
    c.a1 = (g - 1.0) * inv;
    c.a2 = 0.0;
    return c;
}

double butterworthQ(int order, int sectionIndex) {
    return 1.0 / (2.0 * std::cos((2.0 * sectionIndex - 1.0) * kPi / (2.0 * order)));
}

void makeButterworthLowPass(double f, double fs, int order, CoefficientSet& out) {
    out.clear();
    if (order <= 1) {
        out.push(makeFirstOrderLowPass(f, fs));
        return;
    }
    for (int k = 1; k <= order / 2; ++k)
        out.push(makeLowPassSection(f, fs, butterworthQ(order, k)));
}

void makeButterworthHighPass(double f, double fs, int order, CoefficientSet& out) {
    out.clear();
    if (order <= 1) {
        out.push(makeFirstOrderHighPass(f, fs));
        return;
    }
    for (int k = 1; k <= order / 2; ++k)
        out.push(makeHighPassSection(f, fs, butterworthQ(order, k)));
}

CoefficientSet buildBandCoefficients(const BandParams& p, double sampleRate) {
    CoefficientSet set;
    const double f = clampFreq(p.frequency, sampleRate);
    const double q = std::clamp(p.q, 1.0e-3, 1.0e3);

    switch (p.type) {
        case FilterType::Bell:      set.push(makeBell(f, sampleRate, p.gainDb, q)); break;
        case FilterType::LowShelf:  set.push(makeLowShelf(f, sampleRate, p.gainDb, q)); break;
        case FilterType::HighShelf: set.push(makeHighShelf(f, sampleRate, p.gainDb, q)); break;
        case FilterType::Notch:     set.push(makeNotch(f, sampleRate, q)); break;
        case FilterType::BandPass:  set.push(makeBandPass(f, sampleRate, q)); break;
        case FilterType::AllPass:   set.push(makeAllPass(f, sampleRate, q)); break;
        case FilterType::LowPass:   makeButterworthLowPass(f, sampleRate, slopeOrder(p.slope), set); break;
        case FilterType::HighPass:  makeButterworthHighPass(f, sampleRate, slopeOrder(p.slope), set); break;
        default: break;
    }
    return set;
}

double setMagnitude(const CoefficientSet& set, double cosW, double sinW,
                    double cos2W, double sin2W) noexcept {
    double m = 1.0;
    for (int i = 0; i < set.numSections; ++i)
        m *= biquadMagnitude(set.sections[i], cosW, sinW, cos2W, sin2W);
    return m;
}

double setMagnitudeDb(const CoefficientSet& set, double freqHz, double sampleRate) noexcept {
    const double w = 2.0 * kPi * clampFreq(freqHz, sampleRate) / sampleRate;
    const double cosW = std::cos(w), sinW = std::sin(w);
    return gainToDb(setMagnitude(set, cosW, sinW, cosW * cosW - sinW * sinW, 2.0 * sinW * cosW));
}

double bandMagnitudeDb(const BandParams& p, double freqHz, double sampleRate) {
    if (!p.enabled)
        return 0.0;
    return setMagnitudeDb(buildBandCoefficients(p, sampleRate), freqHz, sampleRate);
}

}  // namespace eqm1
