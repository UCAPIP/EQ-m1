// Framework-free tests for the EQ-m1 DSP core. Build: see CMake target EQM1Tests.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "DSP/Biquad.h"
#include "DSP/Curve.h"
#include "DSP/EqEngine.h"

using namespace eqm1;

namespace {

int gPassed = 0;
int gFailed = 0;

void section(const char* name) {
    std::printf("\n[%s]\n", name);
}

void report(bool ok, const char* what, const std::string& detail) {
    if (ok) {
        ++gPassed;
        std::printf("  ok   %s\n", what);
    } else {
        ++gFailed;
        std::printf("  FAIL %s  (%s)\n", what, detail.c_str());
    }
}

void checkNear(double actual, double expected, double tol, const char* what) {
    const bool ok = std::fabs(actual - expected) <= tol;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "got %.6f, want %.6f +/- %.6f", actual, expected, tol);
    report(ok, what, buf);
}

void checkTrue(bool cond, const char* what, const std::string& detail = "") {
    report(cond, what, detail);
}

double magDb(const CoefficientSet& set, double f, double fs) {
    return setMagnitudeDb(set, f, fs);
}

double rms(const std::vector<float>& v, size_t skip) {
    double s = 0.0;
    size_t n = 0;
    for (size_t i = skip; i < v.size(); ++i) {
        s += static_cast<double>(v[i]) * v[i];
        ++n;
    }
    return n > 0 ? std::sqrt(s / static_cast<double>(n)) : 0.0;
}

// Peak amplitude of a steady sine, measured through its RMS so that the result does
// not depend on where the sample grid happens to land within a period.
double amplitude(const std::vector<float>& v, size_t skip) {
    return rms(v, skip) * std::sqrt(2.0);
}

double levelDb(const std::vector<float>& v, size_t skip, double refAmp) {
    return 20.0 * std::log10(std::max(amplitude(v, skip) / refAmp, 1.0e-12));
}

std::vector<float> noise(size_t n, uint32_t seed = 12345u) {
    std::vector<float> v(n);
    uint32_t s = seed;
    for (size_t i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        v[i] = static_cast<float>(static_cast<double>(s) / 2147483648.0 - 1.0);
    }
    return v;
}

std::vector<float> sine(size_t n, double freq, double fs, double amp = 0.5) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = static_cast<float>(amp * std::sin(2.0 * kPi * freq * static_cast<double>(i) / fs));
    return v;
}

struct EngineRun {
    std::vector<float> outL, outR;
};

// setParameters() before prepare() so that every smoother starts snapped to the target
// and the measurement window contains no parameter glide.
EngineRun runEngine(const EngineInput& input, const std::vector<float>& inL,
                    const std::vector<float>& inR, double fs) {
    EqEngine engine;
    engine.setParameters(input);
    engine.prepare(fs);

    EngineRun r;
    r.outL.assign(inL.size(), 0.0f);
    r.outR.assign(inR.size(), 0.0f);

    const float* inPtrs[2] = {inL.data(), inR.data()};
    float* outPtrs[2] = {r.outL.data(), r.outR.data()};
    engine.process(inPtrs, outPtrs, static_cast<int>(inL.size()));
    return r;
}

EngineInput makeInput() {
    EngineInput in;
    in.numBands = kMaxBands;
    for (auto& b : in.bands)
        b = BandParams{};
    in.globals = GlobalParams{};
    return in;
}

double maxDiff(const std::vector<float>& a, const std::vector<float>& b, size_t skip) {
    double worst = 0.0;
    for (size_t i = skip; i < a.size(); ++i)
        worst = std::max(worst, std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i])));
    return worst;
}

//-----------------------------------------------------------------------------
void testBiquadDesigns() {
    section("biquad designs");
    const double fs = 48000.0;

    CoefficientSet bell;
    bell.push(makeBell(1000.0, fs, 6.0, 1.0));
    checkNear(magDb(bell, 1000.0, fs), 6.0, 0.01, "bell: +6 dB at centre");
    checkNear(magDb(bell, 20.0, fs), 0.0, 0.05, "bell: 0 dB at 20 Hz");
    checkNear(magDb(bell, 20000.0, fs), 0.0, 0.05, "bell: 0 dB at 20 kHz");
    checkNear(magDb(bell, 500.0, fs), magDb(bell, 2000.0, fs), 0.05,
              "bell: symmetric one octave either side");
    checkTrue(magDb(bell, 500.0, fs) > 0.0 && magDb(bell, 500.0, fs) < 6.0,
              "bell: skirts stay between 0 dB and the peak", std::to_string(magDb(bell, 500.0, fs)));

    CoefficientSet negBell;
    negBell.push(makeBell(1000.0, fs, -12.0, 1.0));
    checkNear(magDb(negBell, 1000.0, fs), -12.0, 0.01, "bell: -12 dB at centre");

    CoefficientSet narrow = bell;
    narrow.clear();
    narrow.push(makeBell(1000.0, fs, 6.0, 8.0));
    checkTrue(magDb(narrow, 800.0, fs) < magDb(bell, 800.0, fs),
              "bell: higher Q means a narrower skirt");

    CoefficientSet ls;
    ls.push(makeLowShelf(200.0, fs, 9.0, 0.707));
    checkNear(magDb(ls, 20.0, fs), 9.0, 0.2, "low shelf: gain at DC");
    checkNear(magDb(ls, 20000.0, fs), 0.0, 0.1, "low shelf: flat at HF");

    CoefficientSet hs;
    hs.push(makeHighShelf(8000.0, fs, -9.0, 0.707));
    checkNear(magDb(hs, 20000.0, fs), -9.0, 0.3, "high shelf: gain at HF");
    checkNear(magDb(hs, 100.0, fs), 0.0, 0.1, "high shelf: flat at LF");

    CoefficientSet notch;
    notch.push(makeNotch(1000.0, fs, 4.0));
    checkTrue(magDb(notch, 1000.0, fs) < -70.0, "notch: deep at centre",
              std::to_string(magDb(notch, 1000.0, fs)));
    checkNear(magDb(notch, 100.0, fs), 0.0, 0.05, "notch: flat away from centre");

    CoefficientSet bp;
    bp.push(makeBandPass(1000.0, fs, 2.0));
    checkNear(magDb(bp, 1000.0, fs), 0.0, 0.01, "band pass: 0 dB at centre");
    checkTrue(magDb(bp, 100.0, fs) < -20.0, "band pass: rejects far below",
              std::to_string(magDb(bp, 100.0, fs)));
    checkTrue(magDb(bp, 10000.0, fs) < -20.0, "band pass: rejects far above",
              std::to_string(magDb(bp, 10000.0, fs)));

    CoefficientSet ap;
    ap.push(makeAllPass(1000.0, fs, 0.707));
    for (double f : {50.0, 500.0, 1000.0, 5000.0, 18000.0})
        checkNear(magDb(ap, f, fs), 0.0, 1.0e-6, "all pass: unity magnitude");

    checkNear(butterworthQ(2, 1), 0.70710678, 1.0e-6, "butterworth Q: 2nd order");
    checkNear(butterworthQ(4, 1), 0.54119610, 1.0e-6, "butterworth Q: 4th order, section 1");
    checkNear(butterworthQ(4, 2), 1.30656296, 1.0e-6, "butterworth Q: 4th order, section 2");
    checkNear(butterworthQ(8, 4), 2.56291545, 1.0e-6, "butterworth Q: 8th order, section 4");
}

// Slope is measured one octave apart, eight to sixteen times away from the corner,
// where the response is already asymptotic and bilinear warping is still negligible.
void testSlopes() {
    section("low pass / high pass slopes");
    const double fs = 48000.0;
    const double fc = 250.0;
    const double f1 = fc * 8.0;    // 2 kHz
    const double f2 = fc * 16.0;   // 4 kHz

    CoefficientSet lp6;
    makeButterworthLowPass(fc, fs, 1, lp6);
    checkTrue(lp6.numSections == 1, "6 dB/oct uses one section");
    checkNear(magDb(lp6, fc, fs), -3.01, 0.05, "6 dB/oct: -3 dB at corner");
    checkNear(magDb(lp6, f2, fs) - magDb(lp6, f1, fs), -6.0, 0.5, "6 dB/oct asymptotic slope");

    CoefficientSet lp12;
    makeButterworthLowPass(fc, fs, 2, lp12);
    checkNear(magDb(lp12, fc, fs), -3.01, 0.05, "12 dB/oct: -3 dB at corner");
    checkNear(magDb(lp12, f2, fs) - magDb(lp12, f1, fs), -12.0, 0.6, "12 dB/oct asymptotic slope");

    CoefficientSet lp24;
    makeButterworthLowPass(fc, fs, 4, lp24);
    checkTrue(lp24.numSections == 2, "24 dB/oct uses two sections");
    checkNear(magDb(lp24, fc, fs), -3.01, 0.05, "24 dB/oct: -3 dB at corner");
    checkNear(magDb(lp24, f2, fs) - magDb(lp24, f1, fs), -24.0, 1.2, "24 dB/oct asymptotic slope");

    CoefficientSet lp48;
    makeButterworthLowPass(fc, fs, 8, lp48);
    checkTrue(lp48.numSections == 4, "48 dB/oct uses four sections");
    checkNear(magDb(lp48, fc, fs), -3.01, 0.05, "48 dB/oct: -3 dB at corner");
    checkNear(magDb(lp48, f2, fs) - magDb(lp48, f1, fs), -48.0, 2.0, "48 dB/oct asymptotic slope");

    CoefficientSet lp96;
    makeButterworthLowPass(fc, fs, 16, lp96);
    checkTrue(lp96.numSections == kMaxSections, "96 dB/oct fits in kMaxSections");
    checkNear(magDb(lp96, fc, fs), -3.01, 0.05, "96 dB/oct: -3 dB at corner");
    checkTrue(magDb(lp96, fc * 2.0, fs) < -10.0, "96 dB/oct: steep just above the corner",
              std::to_string(magDb(lp96, fc * 2.0, fs)));
    checkTrue(magDb(lp96, fc * 4.0, fs) < -80.0, "96 dB/oct: two octaves above the corner",
              std::to_string(magDb(lp96, fc * 4.0, fs)));

    // Exact Butterworth magnitude in the warped (bilinear) domain:
    // |H| = 1/sqrt(1 + (fa/fca)^(2N)) where fa = (fs/pi)*tan(pi*f/fs).
    auto warpedButterworthDb = [&](int order, double f) {
        const double fa = (fs / kPi) * std::tan(kPi * f / fs);
        const double fca = (fs / kPi) * std::tan(kPi * fc / fs);
        return -10.0 * std::log10(1.0 + std::pow(fa / fca, 2.0 * order));
    };
    checkNear(magDb(lp24, fc * 10.0, fs), warpedButterworthDb(4, fc * 10.0), 0.05,
              "24 dB/oct matches the warped Butterworth formula at 10x fc");
    checkNear(magDb(lp48, fc * 3.0, fs), warpedButterworthDb(8, fc * 3.0), 0.05,
              "48 dB/oct matches the warped Butterworth formula at 3x fc");
    checkNear(magDb(lp12, fc * 2.0, fs), warpedButterworthDb(2, fc * 2.0), 0.05,
              "12 dB/oct matches the warped Butterworth formula at 2x fc");

    const double fcHp = 4000.0;
    CoefficientSet hp24;
    makeButterworthHighPass(fcHp, fs, 4, hp24);
    checkNear(magDb(hp24, fcHp, fs), -3.01, 0.05, "HP 24 dB/oct: -3 dB at corner");
    checkNear(magDb(hp24, fcHp / 16.0, fs) - magDb(hp24, fcHp / 8.0, fs), -24.0, 1.0,
              "HP 24 dB/oct asymptotic slope");
    checkNear(magDb(hp24, 20000.0, fs), 0.0, 0.01, "HP 24 dB/oct: flat in the pass band");

    CoefficientSet hp48;
    makeButterworthHighPass(fcHp, fs, 8, hp48);
    checkNear(magDb(hp48, fcHp, fs), -3.01, 0.05, "HP 48 dB/oct: -3 dB at corner");
    checkTrue(magDb(hp48, fcHp / 8.0, fs) < -60.0, "HP 48 dB/oct: deep below the corner",
              std::to_string(magDb(hp48, fcHp / 8.0, fs)));

    // Cutoffs must stay exact close to Nyquist thanks to pre-warping.
    CoefficientSet lpNearNyquist;
    makeButterworthLowPass(18000.0, fs, 4, lpNearNyquist);
    checkNear(magDb(lpNearNyquist, 18000.0, fs), -3.01, 0.05, "24 dB/oct: -3 dB at 18 kHz / 48 kHz");
}

void testCascadeAndCurve() {
    section("cascade + magnitude curve");
    const double fs = 48000.0;

    BandParams a = BandParams{};
    a.type = FilterType::Bell; a.frequency = 200.0; a.gainDb = 6.0; a.q = 1.0; a.enabled = true;
    BandParams b = BandParams{};
    b.type = FilterType::Bell; b.frequency = 4000.0; b.gainDb = -4.0; b.q = 1.5; b.enabled = true;

    MagnitudeCurve curve;
    BandParams bands[2] = {a, b};
    curve.update(bands, 2, fs);

    checkNear(curve.at(200.0), 6.0 + bandMagnitudeDb(b, 200.0, fs), 0.01, "cascade: band sum at 200 Hz");
    checkNear(curve.at(4000.0), -4.0 + bandMagnitudeDb(a, 4000.0, fs), 0.01, "cascade: band sum at 4 kHz");

    BandParams off = a;
    off.enabled = false;
    BandParams offBands[2] = {off, b};
    MagnitudeCurve curve2;
    curve2.update(offBands, 2, fs);
    checkNear(curve2.at(200.0), bandMagnitudeDb(b, 200.0, fs), 1.0e-9, "disabled band contributes 0 dB");

    MagnitudeCurve empty;
    BandParams none[1] = {off};
    empty.update(none, 1, fs);
    checkTrue(empty.isEmpty(), "curve reports empty when nothing is enabled");
    checkNear(empty.at(1000.0), 0.0, 1.0e-12, "empty curve is flat");
    checkNear(empty.autoGainDb(), 0.0, 1.0e-12, "empty curve needs no compensation");

    double out[64];
    curve.render(out, 64, 20.0, 20000.0);
    checkTrue(out[0] < 1.0 && out[0] > -1.0, "render: sane value at 20 Hz", std::to_string(out[0]));
    checkNear(out[0], curve.at(20.0), 1.0e-9, "render matches at() at the low edge");
    checkNear(out[63], curve.at(20000.0), 1.0e-9, "render matches at() at the high edge");

    const double ag = curve.autoGainDb();
    checkTrue(ag < 0.0 && ag > -12.0, "auto gain: partial compensation for +6/-4 bells",
              std::to_string(ag));

    BandParams onlyBig[1] = {a};
    onlyBig[0].gainDb = 12.0;
    MagnitudeCurve big;
    big.update(onlyBig, 1, fs);
    const double agBig = big.autoGainDb();
    checkTrue(agBig < 0.0 && agBig > -12.0, "auto gain: within +/-12 dB for a +12 dB bell",
              std::to_string(agBig));

    // Auto gain is defined as what cancels the log-average of the curve.
    double sum = 0.0;
    const double lo = std::log(30.0), hi = std::log(16000.0);
    for (int i = 0; i < 96; ++i) {
        const double t = static_cast<double>(i) / 95.0;
        sum += big.at(std::exp(lo + (hi - lo) * t));
    }
    checkNear(sum / 96.0 + agBig, 0.0, 0.05, "auto gain cancels the mean curve");

    // Compensation must scale with the amount of boost.
    BandParams louder[1] = {a};
    louder[0].gainDb = 24.0;
    MagnitudeCurve louderCurve;
    louderCurve.update(louder, 1, fs);
    checkTrue(louderCurve.autoGainDb() < agBig - 1.0, "auto gain grows with the boost",
              std::to_string(louderCurve.autoGainDb()) + " vs " + std::to_string(agBig));
}

void testSpectrumBinning() {
    section("spectrum binning");
    const double fs = 48000.0;
    const int fftSize = 2048;
    const int numBins = fftSize / 2 + 1;
    std::vector<float> mags(static_cast<size_t>(numBins), 0.0f);

    const double hzPerBin = fs / fftSize;
    const int peakBin = static_cast<int>(std::round(1000.0 / hzPerBin));
    mags[static_cast<size_t>(peakBin)] = static_cast<float>(fftSize / 4.0);  // full-scale sine

    double db[kSpectrumBins];
    binSpectrumToLogBands(mags.data(), numBins, fs, db, kSpectrumBins, kSpectrumMinFreq, kSpectrumMaxFreq);

    int maxIdx = 0;
    for (int i = 1; i < kSpectrumBins; ++i)
        if (db[i] > db[maxIdx])
            maxIdx = i;

    const double peakFreq = frequencyForDisplayBin(maxIdx, kSpectrumBins, kSpectrumMinFreq, kSpectrumMaxFreq);
    checkTrue(peakFreq > 850.0 && peakFreq < 1180.0, "binning: peak lands on the 1 kHz display band",
              std::to_string(peakFreq));
    checkNear(db[maxIdx], 0.0, 0.5, "binning: full-scale sine reads ~0 dB");
    checkTrue(db[0] < -100.0, "binning: empty bands sit at the floor", std::to_string(db[0]));

    // Display bins must be log spaced and monotonically rising.
    const double f0 = frequencyForDisplayBin(0, kSpectrumBins, kSpectrumMinFreq, kSpectrumMaxFreq);
    const double f1 = frequencyForDisplayBin(1, kSpectrumBins, kSpectrumMinFreq, kSpectrumMaxFreq);
    const double f2 = frequencyForDisplayBin(2, kSpectrumBins, kSpectrumMinFreq, kSpectrumMaxFreq);
    checkNear((f1 - f0) / f0, (f2 - f1) / f1, 1.0e-9, "binning: display bins are log spaced");

    std::memset(mags.data(), 0, mags.size() * sizeof(float));
    binSpectrumToLogBands(mags.data(), numBins, fs, db, kSpectrumBins, kSpectrumMinFreq, kSpectrumMaxFreq);
    checkNear(db[10], kSpectrumFloorDb, 1.0e-9, "binning: silence reads the floor");

    // A half-scale sine should read -6 dB.
    mags[static_cast<size_t>(peakBin)] = static_cast<float>(fftSize / 8.0);
    binSpectrumToLogBands(mags.data(), numBins, fs, db, kSpectrumBins, kSpectrumMinFreq, kSpectrumMaxFreq);
    checkNear(db[maxIdx], -6.0, 0.5, "binning: half-scale sine reads -6 dB");
}

void testSoloWidthMath() {
    section("solo width <-> Q");
    for (double oct : {0.05, 0.25, 1.0, 2.0, 8.0}) {
        const double q = qFromOctaveWidth(oct);
        checkNear(octaveWidthFromQ(q), oct, 1.0e-6, "round trip octave width");
    }
    checkTrue(qFromOctaveWidth(0.1) > qFromOctaveWidth(4.0), "narrower width means higher Q");
    checkNear(octaveWidthFromQ(1.41), 1.0, 0.02, "1 octave is roughly Q = 1.41");
}

void testEngineBasics() {
    section("engine: gain, bypass, silence");
    const double fs = 48000.0;
    const size_t n = 48000;
    const size_t skip = 4096;

    {
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::Bell;
        in.bands[0].frequency = 1000.0;
        in.bands[0].gainDb = 6.0;
        in.bands[0].q = 1.0;
        in.bands[0].enabled = true;

        const auto s = sine(n, 1000.0, fs, 0.5);
        const auto r = runEngine(in, s, s, fs);
        checkNear(amplitude(r.outL, skip), 1.0, 0.01, "bell +6 dB doubles a 1 kHz sine");
        checkNear(amplitude(r.outR, skip), 1.0, 0.01, "bell +6 dB applies to both channels");
        checkNear(levelDb(r.outL, skip, 0.5), 6.0, 0.05, "bell +6 dB reads +6 dB");
    }

    {
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::Bell;
        in.bands[0].frequency = 1000.0;
        in.bands[0].gainDb = 6.0;
        in.bands[0].enabled = false;

        const auto s = sine(n, 1000.0, fs, 0.5);
        const auto r = runEngine(in, s, s, fs);
        checkTrue(maxDiff(r.outL, s, skip) < 1.0e-6, "disabled band is transparent",
                  std::to_string(maxDiff(r.outL, s, skip)));
    }

    {
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::Bell;
        in.bands[0].frequency = 1000.0;
        in.bands[0].gainDb = 12.0;
        in.bands[0].enabled = true;
        in.globals.bypass = true;

        const auto s = sine(n, 1000.0, fs, 0.5);
        const auto r = runEngine(in, s, s, fs);
        checkTrue(maxDiff(r.outL, s, skip) < 1.0e-6, "global bypass is transparent",
                  std::to_string(maxDiff(r.outL, s, skip)));
    }

    {
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::Bell;
        in.bands[0].frequency = 1000.0;
        in.bands[0].gainDb = 18.0;
        in.bands[0].enabled = true;
        in.globals.outputGainDb = -6.0;

        const auto s = sine(n, 1000.0, fs, 0.5);
        const auto r = runEngine(in, s, s, fs);
        checkNear(amplitude(r.outL, skip), 0.5 * std::pow(10.0, 12.0 / 20.0), 0.02,
                  "output gain stacks with the band gain");
    }

    {
        EngineInput in = makeInput();
        std::vector<float> z(n, 0.0f);
        const auto r = runEngine(in, z, z, fs);
        bool clean = true;
        for (size_t i = 0; i < n; ++i)
            if (r.outL[i] != 0.0f || r.outR[i] != 0.0f)
                clean = false;
        checkTrue(clean, "silence in -> silence out");
    }

    {
        // Odd block lengths must not read past the sub-block scratch buffers.
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::HighPass;
        in.bands[0].frequency = 500.0;
        in.bands[0].slope = Slope::dB48;
        in.bands[0].enabled = true;
        in.bands[3].type = FilterType::LowShelf;
        in.bands[3].frequency = 200.0;
        in.bands[3].gainDb = -6.0;
        in.bands[3].channel = ChannelMode::SideOnly;
        in.bands[3].enabled = true;

        EqEngine engine;
        engine.setParameters(in);
        engine.prepare(fs);
        for (int len : {1, 7, 31, 32, 33, 100, 511, 512, 2048}) {
            std::vector<float> l = sine(static_cast<size_t>(len), 100.0, fs, 0.5);
            std::vector<float> r = sine(static_cast<size_t>(len), 100.0, fs, -0.5);
            std::vector<float> ol(static_cast<size_t>(len), 0.0f), orr(static_cast<size_t>(len), 0.0f);
            const float* inPtrs[2] = {l.data(), r.data()};
            float* outPtrs[2] = {ol.data(), orr.data()};
            engine.process(inPtrs, outPtrs, len);
            bool finite = true;
            for (int i = 0; i < len; ++i)
                if (!std::isfinite(ol[static_cast<size_t>(i)]) || !std::isfinite(orr[static_cast<size_t>(i)]))
                    finite = false;
            checkTrue(finite, ("finite output for block length " + std::to_string(len)).c_str());
        }
    }

    {
        // Parameter changes mid-stream must stay continuous. The reference run holds a
        // static +18 dB, so its per-sample slew is the natural maximum for this signal;
        // a click would show up as a step well beyond that.
        const int total = 48000;
        const int blockSize = 128;
        const int numBlocks = total / blockSize;
        const auto sig = sine(static_cast<size_t>(total), 1000.0, fs, 0.5);

        struct RunResult {
            double maxStep = 0.0;
            double peak = 0.0;
            bool finite = true;
        };

        auto run = [&](const std::function<void(EngineInput&, int)>& perBlock,
                       bool enabledFromStart, double startGain) {
            EngineInput in = makeInput();
            in.bands[0].type = FilterType::Bell;
            in.bands[0].frequency = 1000.0;
            in.bands[0].q = 1.0;
            in.bands[0].gainDb = startGain;
            in.bands[0].enabled = enabledFromStart;

            EqEngine engine;
            engine.setParameters(in);
            engine.prepare(fs);

            std::vector<float> out(static_cast<size_t>(total), 0.0f);
            RunResult r;
            double prev = 0.0;

            for (int b = 0; b < numBlocks; ++b) {
                perBlock(in, b);
                engine.setParameters(in);

                const float* bi[2] = {sig.data() + b * blockSize, sig.data() + b * blockSize};
                float* bo[2] = {out.data() + b * blockSize, out.data() + b * blockSize};
                engine.process(bi, bo, blockSize);

                for (int i = b * blockSize; i < (b + 1) * blockSize; ++i) {
                    const double v = static_cast<double>(out[static_cast<size_t>(i)]);
                    if (!std::isfinite(v))
                        r.finite = false;
                    r.maxStep = std::max(r.maxStep, std::fabs(v - prev));
                    r.peak = std::max(r.peak, std::fabs(v));
                    prev = v;
                }
            }
            return r;
        };

        const auto keep = [](EngineInput&, int) {};
        const RunResult staticRun = run(keep, true, 18.0);

        const RunResult gainSweep = run(
            [numBlocks](EngineInput& in, int b) {
                in.bands[0].gainDb =
                    18.0 * static_cast<double>(b) / static_cast<double>(numBlocks - 1);
            },
            true, 0.0);

        const RunResult enableLate = run(
            [](EngineInput& in, int b) {
                in.bands[0].enabled = (b >= 20);
                in.bands[0].gainDb = 18.0;
            },
            false, 18.0);

        const RunResult freqSweep = run(
            [numBlocks](EngineInput& in, int b) {
                const double t = static_cast<double>(b) / static_cast<double>(numBlocks - 1);
                in.bands[0].frequency = 200.0 * std::pow(50.0, t);   // 200 Hz -> 10 kHz
                in.bands[0].gainDb = 6.0;
            },
            true, 6.0);

        const RunResult typeChange = run(
            [numBlocks](EngineInput& in, int b) {
                in.bands[0].type = (b < numBlocks / 2) ? FilterType::Bell : FilterType::LowPass;
                in.bands[0].slope = Slope::dB24;
                in.bands[0].gainDb = 0.0;
            },
            true, 0.0);

        checkTrue(staticRun.finite && gainSweep.finite && enableLate.finite && freqSweep.finite
                      && typeChange.finite,
                  "automation runs stay finite");
        checkNear(staticRun.peak, 0.5 * std::pow(10.0, 18.0 / 20.0), 0.05,
                  "reference run reaches +18 dB");
        checkTrue(gainSweep.maxStep <= staticRun.maxStep * 1.35, "gain automation does not click",
                  "sweep=" + std::to_string(gainSweep.maxStep)
                      + " static=" + std::to_string(staticRun.maxStep));
        checkTrue(enableLate.maxStep <= staticRun.maxStep * 1.35, "enabling a band does not click",
                  "enable=" + std::to_string(enableLate.maxStep)
                      + " static=" + std::to_string(staticRun.maxStep));
        checkTrue(freqSweep.maxStep <= staticRun.maxStep, "frequency sweep stays inside the static slew",
                  "sweep=" + std::to_string(freqSweep.maxStep)
                      + " static=" + std::to_string(staticRun.maxStep));
        checkTrue(typeChange.maxStep <= staticRun.maxStep * 2.0, "filter type change stays bounded",
                  "change=" + std::to_string(typeChange.maxStep)
                      + " static=" + std::to_string(staticRun.maxStep));
    }
}

void testEngineFiltering() {
    section("engine: filter behaviour");
    const double fs = 48000.0;
    const size_t n = 48000;
    const size_t skip = 8192;

    {
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::HighPass;
        in.bands[0].frequency = 1000.0;
        in.bands[0].slope = Slope::dB24;
        in.bands[0].enabled = true;

        const auto low = sine(n, 50.0, fs, 0.5);
        const auto rl = runEngine(in, low, low, fs);
        checkTrue(levelDb(rl.outL, skip, 0.5) < -60.0, "HP 24 dB/oct kills 50 Hz",
                  std::to_string(levelDb(rl.outL, skip, 0.5)));

        const auto high = sine(n, 8000.0, fs, 0.5);
        const auto rh = runEngine(in, high, high, fs);
        checkNear(amplitude(rh.outL, skip), 0.5, 0.005, "HP passes 8 kHz unchanged");
    }

    {
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::LowPass;
        in.bands[0].frequency = 2000.0;
        in.bands[0].slope = Slope::dB96;
        in.bands[0].enabled = true;

        const auto high = sine(n, 12000.0, fs, 0.5);
        const auto r = runEngine(in, high, high, fs);
        checkTrue(levelDb(r.outL, skip, 0.5) < -80.0, "LP 96 dB/oct kills 12 kHz",
                  std::to_string(levelDb(r.outL, skip, 0.5)));
    }

    {
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::LowShelf;
        in.bands[0].frequency = 150.0;
        in.bands[0].gainDb = -9.0;
        in.bands[0].enabled = true;

        const auto low = sine(n, 40.0, fs, 0.5);
        const auto rl = runEngine(in, low, low, fs);
        checkNear(levelDb(rl.outL, skip, 0.5), -9.0, 0.3, "low shelf cuts 40 Hz by its gain");

        const auto high = sine(n, 9000.0, fs, 0.5);
        const auto rh = runEngine(in, high, high, fs);
        checkNear(levelDb(rh.outL, skip, 0.5), 0.0, 0.3, "low shelf leaves 9 kHz alone");
    }

    {
        // Two bands in series: gains add where both act.
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::Bell;
        in.bands[0].frequency = 1000.0; in.bands[0].gainDb = 6.0; in.bands[0].q = 0.5;
        in.bands[0].enabled = true;
        in.bands[1].type = FilterType::Bell;
        in.bands[1].frequency = 1000.0; in.bands[1].gainDb = -3.0; in.bands[1].q = 0.5;
        in.bands[1].enabled = true;

        const auto s = sine(n, 1000.0, fs, 0.25);
        const auto r = runEngine(in, s, s, fs);
        checkNear(levelDb(r.outL, skip, 0.25), 3.0, 0.1, "cascade: +6 dB and -3 dB stack to +3 dB");
    }

    {
        // Every enabled band type must stay finite and sane on broadband material.
        const auto sig = noise(24000);
        for (int t = 0; t < static_cast<int>(FilterType::count); ++t) {
            for (int s = 0; s < static_cast<int>(Slope::count); ++s) {
                EngineInput in = makeInput();
                in.bands[0].type = static_cast<FilterType>(t);
                in.bands[0].slope = static_cast<Slope>(s);
                in.bands[0].frequency = 1200.0;
                in.bands[0].gainDb = 9.0;
                in.bands[0].q = 2.0;
                in.bands[0].enabled = true;
                const auto r = runEngine(in, sig, sig, fs);

                bool finite = true;
                for (size_t i = 0; i < r.outL.size(); ++i)
                    if (!std::isfinite(r.outL[i]))
                        finite = false;

                const std::string label = std::string(filterTypeName(static_cast<FilterType>(t))) + "/"
                                        + slopeName(static_cast<Slope>(s));
                checkTrue(finite, ("finite output for " + label).c_str());
                checkTrue(rms(r.outL, 8192) < 4.0, ("no runaway gain for " + label).c_str(),
                          std::to_string(rms(r.outL, 8192)));
            }
        }
    }
}

void testChannelModes() {
    section("engine: channel modes");
    const double fs = 48000.0;
    const size_t n = 24000;
    const size_t skip = 4096;

    auto bellAt1k = [](ChannelMode mode) {
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::Bell;
        in.bands[0].frequency = 1000.0;
        in.bands[0].gainDb = 6.0;
        in.bands[0].q = 1.0;
        in.bands[0].channel = mode;
        in.bands[0].enabled = true;
        return in;
    };

    {
        const auto L = sine(n, 1000.0, fs, 0.5);
        const auto R = sine(n, 1000.0, fs, 0.25);
        const auto r = runEngine(bellAt1k(ChannelMode::LeftOnly), L, R, fs);
        checkNear(amplitude(r.outL, skip), 1.0, 0.01, "L only: left is boosted");
        checkTrue(maxDiff(r.outR, R, skip) < 1.0e-6, "L only: right untouched",
                  std::to_string(maxDiff(r.outR, R, skip)));
    }

    {
        const auto L = sine(n, 1000.0, fs, 0.5);
        const auto R = sine(n, 1000.0, fs, 0.25);
        const auto r = runEngine(bellAt1k(ChannelMode::RightOnly), L, R, fs);
        checkNear(amplitude(r.outR, skip), 0.5, 0.01, "R only: right is boosted");
        checkTrue(maxDiff(r.outL, L, skip) < 1.0e-6, "R only: left untouched",
                  std::to_string(maxDiff(r.outL, L, skip)));
    }

    {
        // Dual mono is pure mid, so a mid-only +6 dB bell doubles both channels.
        const auto s = sine(n, 1000.0, fs, 0.4);
        const auto r = runEngine(bellAt1k(ChannelMode::MidOnly), s, s, fs);
        checkNear(amplitude(r.outL, skip), 0.8, 0.01, "Mid only: dual mono gains +6 dB");
        checkNear(amplitude(r.outR, skip), 0.8, 0.01, "Mid only: right follows");
        checkTrue(maxDiff(r.outL, r.outR, skip) < 1.0e-6, "Mid only: output stays dual mono");
    }

    {
        // Pure side (L = -R): a side-only +6 dB bell doubles both channels.
        const auto L = sine(n, 1000.0, fs, 0.4);
        std::vector<float> R(n);
        for (size_t i = 0; i < n; ++i)
            R[i] = -L[i];
        const auto r = runEngine(bellAt1k(ChannelMode::SideOnly), L, R, fs);
        checkNear(amplitude(r.outL, skip), 0.8, 0.01, "Side only: pure side gains +6 dB");
        bool mirrored = true;
        for (size_t i = skip; i < n; ++i)
            if (std::fabs(r.outL[i] + r.outR[i]) > 1.0e-5f)
                mirrored = false;
        checkTrue(mirrored, "Side only: output stays anti-phase");
    }

    {
        // A side-only bell must not touch a dual-mono signal at all.
        const auto s = sine(n, 1000.0, fs, 0.4);
        const auto r = runEngine(bellAt1k(ChannelMode::SideOnly), s, s, fs);
        checkTrue(maxDiff(r.outL, s, skip) < 1.0e-5, "Side only: dual mono untouched",
                  std::to_string(maxDiff(r.outL, s, skip)));
    }

    {
        // A mid-only bell must not touch a pure side signal at all.
        const auto L = sine(n, 1000.0, fs, 0.4);
        std::vector<float> R(n);
        for (size_t i = 0; i < n; ++i)
            R[i] = -L[i];
        const auto r = runEngine(bellAt1k(ChannelMode::MidOnly), L, R, fs);
        checkTrue(maxDiff(r.outL, L, skip) < 1.0e-5, "Mid only: pure side untouched",
                  std::to_string(maxDiff(r.outL, L, skip)));
    }
}

void testSoloMonitoring() {
    section("engine: solo monitoring");
    const double fs = 48000.0;
    const size_t n = 48000;
    const size_t skip = 8192;

    auto soloInput = [](double freq, double gainDb, double widthOct) {
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::Bell;
        in.bands[0].frequency = freq;
        in.bands[0].gainDb = gainDb;
        in.bands[0].q = 1.0;
        in.bands[0].enabled = true;
        in.bands[0].solo = true;
        in.globals.soloWidthOct = widthOct;
        return in;
    };

    {
        const auto inBand = sine(n, 1000.0, fs, 0.5);
        const auto r = runEngine(soloInput(1000.0, 0.0, 1.0), inBand, inBand, fs);
        checkNear(levelDb(r.outL, skip, 0.5), 0.0, 1.0, "solo: in-band tone passes at unity");
    }

    {
        const auto inBand = sine(n, 1000.0, fs, 0.5);
        const auto r = runEngine(soloInput(1000.0, 9.0, 1.0), inBand, inBand, fs);
        checkNear(levelDb(r.outL, skip, 0.5), 9.0, 1.0, "solo: the band's own gain is audible");
    }

    {
        const auto outBand = sine(n, 8000.0, fs, 0.5);
        const auto r = runEngine(soloInput(1000.0, 0.0, 0.5), outBand, outBand, fs);
        checkTrue(levelDb(r.outL, skip, 0.5) < -50.0, "solo: 8 kHz is rejected while soloing 1 kHz",
                  std::to_string(levelDb(r.outL, skip, 0.5)));
    }

    {
        const auto outBand = sine(n, 2000.0, fs, 0.5);
        const auto wide = runEngine(soloInput(1000.0, 0.0, 4.0), outBand, outBand, fs);
        const auto narrow = runEngine(soloInput(1000.0, 0.0, 0.25), outBand, outBand, fs);
        const double wideDb = levelDb(wide.outL, skip, 0.5);
        const double narrowDb = levelDb(narrow.outL, skip, 0.5);
        checkTrue(narrowDb < wideDb - 20.0, "solo: narrower width isolates better",
                  "wide=" + std::to_string(wideDb) + " narrow=" + std::to_string(narrowDb));
    }

    {
        // Solo monitoring must ignore every other band.
        EngineInput in = soloInput(1000.0, 0.0, 1.0);
        in.bands[5].type = FilterType::Bell;
        in.bands[5].frequency = 1000.0;
        in.bands[5].gainDb = 24.0;
        in.bands[5].q = 1.0;
        in.bands[5].enabled = true;

        const auto s = sine(n, 1000.0, fs, 0.5);
        const auto r = runEngine(in, s, s, fs);
        checkNear(levelDb(r.outL, skip, 0.5), 0.0, 1.5, "solo: other bands stay out of the solo path");
    }

    {
        // Two soloed bands sum: two disjoint tones both survive.
        EngineInput in = makeInput();
        in.globals.soloWidthOct = 0.3;
        for (int i = 0; i < 2; ++i) {
            in.bands[i].type = FilterType::Bell;
            in.bands[i].q = 1.0;
            in.bands[i].gainDb = 0.0;
            in.bands[i].enabled = true;
            in.bands[i].solo = true;
            in.bands[i].frequency = (i == 0) ? 300.0 : 6000.0;
        }

        std::vector<float> mix(n);
        const auto a = sine(n, 300.0, fs, 0.4);
        const auto b = sine(n, 6000.0, fs, 0.4);
        for (size_t i = 0; i < n; ++i)
            mix[i] = a[i] + b[i];

        const auto r = runEngine(in, mix, mix, fs);
        // Two uncorrelated 0.4 amplitude tones sum to an amplitude of sqrt(0.4^2 + 0.4^2).
        checkNear(amplitude(r.outL, skip), 0.4 * std::sqrt(2.0), 0.04,
                  "solo: two soloed bands both pass");

        const auto onlyA = runEngine(in, a, a, fs);
        checkNear(levelDb(onlyA.outL, skip, 0.4), 0.0, 1.5,
                  "solo: 300 Hz tone passes through the 300 Hz solo band");

        const auto onlyB = runEngine(in, b, b, fs);
        checkNear(levelDb(onlyB.outL, skip, 0.4), 0.0, 1.5,
                  "solo: 6 kHz tone passes through the 6 kHz solo band");
    }

    {
        // Solo must survive a global bypass request being turned off/on and stay finite.
        EngineInput in = soloInput(500.0, 0.0, 1.0);
        in.globals.bypass = true;
        const auto s = sine(n, 500.0, fs, 0.5);
        const auto r = runEngine(in, s, s, fs);
        checkTrue(maxDiff(r.outL, s, skip) < 1.0e-6, "bypass wins over solo",
                  std::to_string(maxDiff(r.outL, s, skip)));
    }
}

void testAutoGain() {
    section("engine: auto gain");
    const double fs = 48000.0;
    const size_t n = 96000;
    const size_t skip = 16384;

    const auto sig = noise(n);

    auto withAuto = [&](bool autoGain) {
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::Bell;
        in.bands[0].frequency = 2500.0;
        in.bands[0].gainDb = 12.0;
        in.bands[0].q = 1.0;
        in.bands[0].enabled = true;
        in.bands[1].type = FilterType::LowShelf;
        in.bands[1].frequency = 120.0;
        in.bands[1].gainDb = 9.0;
        in.bands[1].enabled = true;
        in.globals.autoGain = autoGain;
        return runEngine(in, sig, sig, fs);
    };

    const auto plain = withAuto(false);
    const auto compensated = withAuto(true);

    const double plainRms = rms(plain.outL, skip);
    const double autoRms = rms(compensated.outL, skip);
    checkTrue(plainRms > 0.0, "auto gain: reference run produced signal", std::to_string(plainRms));
    checkTrue(autoRms < plainRms * 0.9, "auto gain lowers the broadband level of a boosted curve",
              "plain=" + std::to_string(plainRms) + " auto=" + std::to_string(autoRms));
    checkTrue(autoRms > 0.0 && std::isfinite(autoRms), "auto gain output is finite");

    {
        EngineInput in = makeInput();
        in.globals.autoGain = true;   // no bands -> no compensation
        const auto s = sine(n, 500.0, fs, 0.5);
        const auto r = runEngine(in, s, s, fs);
        checkNear(amplitude(r.outL, skip), 0.5, 0.005, "auto gain is unity on a flat curve");
    }

    {
        // The engine must expose the compensation it settled on.
        EqEngine engine;
        EngineInput in = makeInput();
        in.bands[0].type = FilterType::Bell;
        in.bands[0].frequency = 1000.0;
        in.bands[0].gainDb = 12.0;
        in.bands[0].q = 1.0;
        in.bands[0].enabled = true;
        in.globals.autoGain = true;
        engine.setParameters(in);
        engine.prepare(fs);
        const double reported = engine.currentAutoGainDb();
        checkTrue(reported < 0.0 && reported > -12.0, "engine reports its auto gain in dB",
                  std::to_string(reported));
    }
}

}  // namespace

int main() {
    std::printf("EQ-m1 DSP core tests\n");

    testBiquadDesigns();
    testSlopes();
    testCascadeAndCurve();
    testSpectrumBinning();
    testSoloWidthMath();
    testEngineBasics();
    testEngineFiltering();
    testChannelModes();
    testSoloMonitoring();
    testAutoGain();

    std::printf("\n-----------------------------------------\n");
    std::printf("passed: %d   failed: %d\n", gPassed, gFailed);
    std::printf(gFailed == 0 ? "RESULT: OK\n" : "RESULT: FAILED\n");
    return gFailed == 0 ? 0 : 1;
}
