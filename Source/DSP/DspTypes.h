#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace eqm1 {

inline constexpr int kMaxBands = 16;
inline constexpr int kNumChannels = 2;
inline constexpr int kMaxButterworthOrder = 16;                 // 96 dB/oct
inline constexpr int kMaxSections = kMaxButterworthOrder / 2;   // biquads per band
inline constexpr int kSoloSections = 2;                         // 4th order solo band-pass

inline constexpr double kMinFreq = 10.0;
inline constexpr double kMaxFreq = 30000.0;
inline constexpr double kMinGainDb = -30.0;
inline constexpr double kMaxGainDb = 30.0;
inline constexpr double kMinQ = 0.05;
inline constexpr double kMaxQ = 24.0;
inline constexpr double kMinSoloWidthOct = 0.05;
inline constexpr double kMaxSoloWidthOct = 8.0;

inline constexpr double kPi = 3.14159265358979323846;

enum class FilterType : int {
    Bell = 0,
    LowShelf,
    HighShelf,
    LowPass,
    HighPass,
    Notch,
    BandPass,
    AllPass,
    count
};

enum class ChannelMode : int {
    Stereo = 0,
    LeftOnly,
    RightOnly,
    MidOnly,
    SideOnly,
    count
};

// Filter order for LowPass/HighPass, expressed as dB per octave.
enum class Slope : int {
    dB6 = 0,
    dB12,
    dB24,
    dB48,
    dB96,
    count
};

inline int slopeOrder(Slope s) noexcept { return 1 << static_cast<int>(s); }

inline bool hasGain(FilterType t) noexcept {
    return t == FilterType::Bell || t == FilterType::LowShelf || t == FilterType::HighShelf;
}

struct BandParams {
    FilterType type = FilterType::Bell;
    ChannelMode channel = ChannelMode::Stereo;
    Slope slope = Slope::dB24;
    double frequency = 1000.0;
    double gainDb = 0.0;
    double q = 0.707;
    bool enabled = false;
    bool solo = false;

    bool usesGain() const noexcept { return hasGain(type); }
    bool usesQ() const noexcept { return type != FilterType::LowPass && type != FilterType::HighPass; }
};

struct GlobalParams {
    double outputGainDb = 0.0;
    double soloWidthOct = 1.0;
    bool autoGain = false;
    bool bypass = false;
};

struct EngineInput {
    BandParams bands[kMaxBands];
    GlobalParams globals;
    int numBands = kMaxBands;
};

inline bool operator==(const BandParams& a, const BandParams& b) noexcept {
    return a.type == b.type && a.channel == b.channel && a.slope == b.slope
        && a.frequency == b.frequency && a.gainDb == b.gainDb && a.q == b.q
        && a.enabled == b.enabled && a.solo == b.solo;
}

inline bool operator==(const GlobalParams& a, const GlobalParams& b) noexcept {
    return a.outputGainDb == b.outputGainDb && a.soloWidthOct == b.soloWidthOct
        && a.autoGain == b.autoGain && a.bypass == b.bypass;
}

inline bool operator==(const EngineInput& a, const EngineInput& b) noexcept {
    if (a.numBands != b.numBands || !(a.globals == b.globals))
        return false;
    for (int i = 0; i < kMaxBands; ++i) {
        if (!(a.bands[i] == b.bands[i]))
            return false;
    }
    return true;
}

inline bool operator!=(const EngineInput& a, const EngineInput& b) noexcept { return !(a == b); }

inline double dbToGain(double db) noexcept { return std::pow(10.0, db / 20.0); }
inline double gainToDb(double gain) noexcept { return 20.0 * std::log10(std::max(gain, 1.0e-12)); }

inline double clampFreq(double f, double sampleRate) noexcept {
    const double nyquistLimit = sampleRate > 0.0 ? sampleRate * 0.495 : kMaxFreq;
    return std::clamp(f, kMinFreq, std::min(kMaxFreq, nyquistLimit));
}

// Q of a 2nd order band-pass whose -3 dB width spans `octaves` around the centre frequency.
inline double qFromOctaveWidth(double octaves) noexcept {
    return 1.0 / (2.0 * std::sinh(0.5 * std::log(2.0) * std::clamp(octaves, 1.0e-3, 12.0)));
}

inline double octaveWidthFromQ(double q) noexcept {
    if (q <= 1.0e-6)
        return 12.0;
    return 2.0 * std::asinh(1.0 / (2.0 * q)) / std::log(2.0);
}

inline const char* filterTypeName(FilterType t) noexcept {
    switch (t) {
        case FilterType::Bell:      return "Bell";
        case FilterType::LowShelf:  return "Low Shelf";
        case FilterType::HighShelf: return "High Shelf";
        case FilterType::LowPass:   return "Low Pass";
        case FilterType::HighPass:  return "High Pass";
        case FilterType::Notch:     return "Notch";
        case FilterType::BandPass:  return "Band Pass";
        case FilterType::AllPass:   return "All Pass";
        default:                    return "?";
    }
}

inline const char* channelModeName(ChannelMode m) noexcept {
    switch (m) {
        case ChannelMode::Stereo:   return "Stereo";
        case ChannelMode::LeftOnly: return "L only";
        case ChannelMode::RightOnly:return "R only";
        case ChannelMode::MidOnly:  return "Mid only";
        case ChannelMode::SideOnly: return "Side only";
        default:                    return "?";
    }
}

inline const char* slopeName(Slope s) noexcept {
    switch (s) {
        case Slope::dB6:  return "6 dB/oct";
        case Slope::dB12: return "12 dB/oct";
        case Slope::dB24: return "24 dB/oct";
        case Slope::dB48: return "48 dB/oct";
        case Slope::dB96: return "96 dB/oct";
        default:          return "?";
    }
}

}  // namespace eqm1
