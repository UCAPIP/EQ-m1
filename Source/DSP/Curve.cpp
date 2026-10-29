#include "DSP/Curve.h"

namespace eqm1 {

void MagnitudeCurve::update(const BandParams* bands, int numBands, double sampleRate) {
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    count = std::clamp(numBands, 0, kMaxBands);
    empty = true;

    for (int i = 0; i < count; ++i) {
        const bool isActive = bands[i].enabled || bands[i].solo;
        active[static_cast<size_t>(i)] = isActive;
        if (isActive) {
            sets[static_cast<size_t>(i)] = buildBandCoefficients(bands[i], fs);
            empty = false;
        } else {
            sets[static_cast<size_t>(i)].clear();
        }
    }
}

double MagnitudeCurve::at(double freqHz) const {
    if (empty)
        return 0.0;

    const double w = 2.0 * kPi * clampFreq(freqHz, fs) / fs;
    const double cosW = std::cos(w);
    const double sinW = std::sin(w);
    const double cos2W = cosW * cosW - sinW * sinW;
    const double sin2W = 2.0 * sinW * cosW;

    double m = 1.0;
    for (int i = 0; i < count; ++i) {
        if (active[static_cast<size_t>(i)])
            m *= setMagnitude(sets[static_cast<size_t>(i)], cosW, sinW, cos2W, sin2W);
    }
    return gainToDb(m);
}

void MagnitudeCurve::render(double* outDb, int numPoints, double fMin, double fMax) const {
    if (numPoints <= 0 || outDb == nullptr)
        return;

    const double lo = std::log(std::max(fMin, 1.0));
    const double hi = std::log(std::max(fMax, fMin + 1.0));

    for (int i = 0; i < numPoints; ++i) {
        const double t = numPoints > 1 ? static_cast<double>(i) / (numPoints - 1) : 0.0;
        outDb[i] = at(std::exp(lo + (hi - lo) * t));
    }
}

double MagnitudeCurve::autoGainDb(int numPoints) const {
    if (empty)
        return 0.0;

    const double points = static_cast<double>(std::clamp(numPoints, 8, 1024));
    const double lo = std::log(30.0);
    const double hi = std::log(std::min(16000.0, fs * 0.45));
    if (hi <= lo)
        return 0.0;

    double sum = 0.0;
    for (int i = 0; i < static_cast<int>(points); ++i) {
        const double t = static_cast<double>(i) / (points - 1.0);
        sum += at(std::exp(lo + (hi - lo) * t));
    }

    return std::clamp(-sum / points, -24.0, 24.0);
}

void binSpectrumToLogBands(const float* magnitudes, int numBins, double sampleRate,
                           double* outDb, int numOutBins, double fMin, double fMax) {
    if (magnitudes == nullptr || outDb == nullptr || numBins < 2 || numOutBins < 1 || sampleRate <= 0.0)
        return;

    const double fftSize = 2.0 * static_cast<double>(numBins - 1);
    const double hzPerBin = sampleRate / fftSize;
    const double nyquist = sampleRate * 0.5;
    const double top = std::min(fMax, nyquist);
    const double lo = std::log(std::max(fMin, 1.0));
    const double hi = std::log(std::max(top, fMin * 1.01));

    // Hann coherent gain is 0.5, hence the factor 4 (= 2 / 0.5) to recover amplitude.
    const double norm = 4.0 / fftSize;

    for (int i = 0; i < numOutBins; ++i) {
        const double f0 = std::exp(lo + (hi - lo) * static_cast<double>(i) / numOutBins);
        const double f1 = std::exp(lo + (hi - lo) * static_cast<double>(i + 1) / numOutBins);

        int k0 = static_cast<int>(std::floor(f0 / hzPerBin));
        int k1 = static_cast<int>(std::ceil(f1 / hzPerBin));
        k0 = std::clamp(k0, 0, numBins - 1);
        k1 = std::clamp(k1, k0 + 1, numBins);

        // Peak hold across the display band: a sine should read its own amplitude
        // instead of being smeared by the averaging that broadband noise wants.
        double peak = 0.0;
        for (int k = k0; k < k1; ++k)
            peak = std::max(peak, static_cast<double>(magnitudes[k]));

        outDb[i] = std::max(gainToDb(peak * norm), kSpectrumFloorDb);
    }
}

double frequencyForDisplayBin(int binIndex, int numBins, double fMin, double fMax) {
    if (numBins <= 0)
        return fMin;
    const double lo = std::log(std::max(fMin, 1.0));
    const double hi = std::log(std::max(fMax, fMin * 1.01));
    return std::exp(lo + (hi - lo) * (static_cast<double>(binIndex) + 0.5) / static_cast<double>(numBins));
}

}  // namespace eqm1
