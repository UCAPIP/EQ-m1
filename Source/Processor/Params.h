#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>

#include "DSP/DspTypes.h"

namespace eqm1 {

using Parameter = juce::AudioProcessorParameter;
using ParameterLayout = juce::AudioProcessorValueTreeState::ParameterLayout;

// Kept as a plain constant rather than JucePlugin_Name so that the same sources can be
// compiled both into the plugin and into the headless integration test binary.
inline constexpr const char* kPluginName = "EQ-m1";

// --- parameter IDs ---------------------------------------------------------
inline juce::String bandParamId(int band, const char* suffix) {
    return "b" + juce::String(band) + "_" + suffix;
}

namespace ids {
static const juce::String kOutGain{"out_gain"};
static const juce::String kAutoGain{"auto_gain"};
static const juce::String kBypass{"bypass"};
static const juce::String kSoloWidth{"solo_width"};
static const juce::String kAnalyzerOn{"analyzer_on"};
static const juce::String kAnalyzerRange{"analyzer_range"};
static const juce::String kAnalyzerSpeed{"analyzer_speed"};

static const char* const kType = "type";
static const char* const kFreq = "freq";
static const char* const kGain = "gain";
static const char* const kQ = "q";
static const char* const kSlope = "slope";
static const char* const kChannel = "ch";
static const char* const kOn = "on";
static const char* const kSolo = "solo";
}  // namespace ids

// --- ranges ----------------------------------------------------------------
// Exact logarithmic mapping between a real value and its 0..1 normalised form.
// JUCE 8 takes the range bounds as arguments so that one lambda serves any span.
inline juce::NormalisableRange<float> logRange(float start, float end) {
    return juce::NormalisableRange<float>(
        start, end,
        [](float s, float e, float n) {
            return static_cast<float>(s * std::pow(static_cast<double>(e) / s, juce::jlimit(0.0f, 1.0f, n)));
        },
        [](float s, float e, float v) {
            return static_cast<float>(std::log(static_cast<double>(juce::jmax(v, s)) / s)
                                      / std::log(static_cast<double>(e) / s));
        });
}

inline juce::NormalisableRange<float> freqRange()      { return logRange(10.0f, 30000.0f); }
inline juce::NormalisableRange<float> qRange()         { return logRange(0.05f, 24.0f); }
inline juce::NormalisableRange<float> soloWidthRange() { return logRange(0.05f, 8.0f); }

inline juce::NormalisableRange<float> gainRange() {
    return juce::NormalisableRange<float>(-30.0f, 30.0f, 0.01f);
}

inline juce::NormalisableRange<float> outputGainRange() {
    return juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f);
}

// --- value formatting ------------------------------------------------------
inline juce::String freqText(float v) {
    if (v >= 1000.0f)
        return juce::String(v / 1000.0f, 2) + " kHz";
    return juce::String(v, v >= 100.0f ? 0 : 1) + " Hz";
}

inline juce::String gainText(float v) {
    return (v > 0.0f ? "+" : "") + juce::String(v, 1) + " dB";
}

inline juce::String qText(float v) {
    return v >= 10.0f ? juce::String(v, 1) : juce::String(v, 2);
}

inline juce::String octText(float v) {
    return juce::String(v, 2) + " oct";
}

inline juce::StringArray filterTypeNames() {
    juce::StringArray a;
    for (int i = 0; i < static_cast<int>(FilterType::count); ++i)
        a.add(filterTypeName(static_cast<FilterType>(i)));
    return a;
}

inline juce::StringArray channelModeNames() {
    juce::StringArray a;
    for (int i = 0; i < static_cast<int>(ChannelMode::count); ++i)
        a.add(channelModeName(static_cast<ChannelMode>(i)));
    return a;
}

inline juce::StringArray slopeNames() {
    juce::StringArray a;
    for (int i = 0; i < static_cast<int>(Slope::count); ++i)
        a.add(slopeName(static_cast<Slope>(i)));
    return a;
}

inline juce::StringArray analyzerRangeNames() { return {"12 dB", "24 dB", "48 dB", "96 dB"}; }
inline juce::StringArray analyzerSpeedNames() { return {"Freeze", "Slow", "Medium", "Fast"}; }

inline double analyzerRangeDb(int index) {
    switch (index) {
        case 0: return 12.0;
        case 1: return 24.0;
        case 3: return 96.0;
        default: return 48.0;
    }
}

// Decay applied per display frame, in dB.
inline double analyzerDecayPerFrame(int speedIndex) {
    switch (speedIndex) {
        case 0: return 0.0;      // freeze
        case 1: return 0.35;
        case 3: return 4.0;
        default: return 1.2;
    }
}

// Default node frequency of each band: a geometric spread across the spectrum.
inline double defaultBandFrequency(int band) {
    return std::min(23000.0, 20.0 * std::pow(1.6, static_cast<double>(band)));
}

// Raw parameter values are stored in real units (Hz, dB, index), so reading them
// directly avoids a denormalise round trip.
inline double getParamRealValue(juce::AudioProcessorValueTreeState& apvts, const juce::String& id) {
    const std::atomic<float>* a = apvts.getRawParameterValue(id);
    return a != nullptr ? static_cast<double>(a->load()) : 0.0;
}

inline void setParamRealValue(juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
                              double realValue) {
    auto* p = apvts.getParameter(id);
    if (p == nullptr)
        return;
    const auto range = apvts.getParameterRange(id);
    // convertTo0to1 asserts on values outside the range, so clip first.
    const float clipped = juce::jlimit(range.start, range.end, static_cast<float>(realValue));
    p->setValueNotifyingHost(p->convertTo0to1(clipped));
}

ParameterLayout createParameterLayout();

// --- factory presets -------------------------------------------------------
struct PresetBand {
    int band;
    FilterType type;
    double frequency;
    double gainDb;
    double q;
    Slope slope;
    ChannelMode channel;
};

struct Preset {
    juce::String name;
    std::vector<PresetBand> bands;
    double outputGainDb = 0.0;
    bool autoGain = false;
};

const std::vector<Preset>& factoryPresets();

}  // namespace eqm1
