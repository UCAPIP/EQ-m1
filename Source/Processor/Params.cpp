#include "Processor/Params.h"

namespace eqm1 {

ParameterLayout createParameterLayout() {
    ParameterLayout layout;

    auto floatParam = [](const juce::String& id, const juce::String& name,
                         juce::NormalisableRange<float> range, float defaultValue,
                         const char* label, juce::String (*format)(float)) {
        return std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1}, name, std::move(range), defaultValue,
            juce::AudioParameterFloatAttributes()
                .withLabel(juce::String(label))
                .withStringFromValueFunction([format](float v, int) { return format(v); }));
    };

    for (int i = 0; i < kMaxBands; ++i) {
        const auto freq = static_cast<float>(defaultBandFrequency(i));
        const juce::String num = juce::String(i + 1);

        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{bandParamId(i, ids::kType), 1}, "Band " + num + " Type",
            filterTypeNames(), 0));

        layout.add(floatParam(bandParamId(i, ids::kFreq), "Band " + num + " Frequency",
                              freqRange(), freq, "Hz", freqText));

        layout.add(floatParam(bandParamId(i, ids::kGain), "Band " + num + " Gain",
                              gainRange(), 0.0f, "dB", gainText));

        layout.add(floatParam(bandParamId(i, ids::kQ), "Band " + num + " Q",
                              qRange(), 0.707f, "", qText));

        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{bandParamId(i, ids::kSlope), 1}, "Band " + num + " Slope",
            slopeNames(), 2));

        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{bandParamId(i, ids::kChannel), 1}, "Band " + num + " Channel",
            channelModeNames(), 0));

        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{bandParamId(i, ids::kOn), 1}, "Band " + num + " Enable", false));

        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{bandParamId(i, ids::kSolo), 1}, "Band " + num + " Solo", false));
    }

    layout.add(floatParam(ids::kOutGain, "Output Gain", outputGainRange(), 0.0f, "dB", gainText));
    layout.add(floatParam(ids::kSoloWidth, "Solo Width", soloWidthRange(), 1.0f, "oct", octText));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ids::kAutoGain, 1}, "Auto Gain", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ids::kBypass, 1}, "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ids::kAnalyzerOn, 1}, "Analyzer", true));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::kAnalyzerRange, 1}, "Analyzer Range", analyzerRangeNames(), 2));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::kAnalyzerSpeed, 1}, "Analyzer Speed", analyzerSpeedNames(), 2));

    return layout;
}

const std::vector<Preset>& factoryPresets() {
    static const std::vector<Preset> presets = [] {
        auto band = [](int index, FilterType type, double freq, double gainDb, double q,
                       Slope slope = Slope::dB24,
                       ChannelMode ch = ChannelMode::Stereo) {
            return PresetBand{index, type, freq, gainDb, q, slope, ch};
        };
        const FilterType bell = FilterType::Bell;
        const FilterType ls = FilterType::LowShelf;
        const FilterType hs = FilterType::HighShelf;
        const FilterType hp = FilterType::HighPass;
        const FilterType lp = FilterType::LowPass;
        const FilterType notch = FilterType::Notch;

        std::vector<Preset> p;

        p.push_back(Preset{"Init", {}, 0.0, false});

        p.push_back(Preset{"Vocal Clarity",
                           {band(0, hp, 85.0, 0.0, 0.707, Slope::dB24),
                            band(1, bell, 300.0, -3.0, 1.2),
                            band(2, bell, 3500.0, 2.5, 1.0),
                            band(3, hs, 10000.0, 2.0, 0.707)},
                           0.0, false});

        p.push_back(Preset{"Bass Weight",
                           {band(0, hp, 28.0, 0.0, 0.707, Slope::dB12),
                            band(1, ls, 90.0, 5.0, 0.707),
                            band(2, bell, 165.0, 3.0, 1.5),
                            band(3, bell, 420.0, -2.0, 1.1)},
                           0.0, true});

        p.push_back(Preset{"Mud Cut",
                           {band(0, bell, 220.0, -3.0, 0.9),
                            band(1, bell, 380.0, -5.0, 1.1)},
                           0.0, false});

        p.push_back(Preset{"Air Lift",
                           {band(0, bell, 6000.0, 1.5, 1.0),
                            band(1, hs, 12000.0, 4.0, 0.707)},
                           0.0, false});

        p.push_back(Preset{"Drum Snap",
                           {band(0, hp, 40.0, 0.0, 0.707, Slope::dB24),
                            band(1, bell, 300.0, -3.0, 1.5),
                            band(2, bell, 4000.0, 4.0, 2.0),
                            band(3, lp, 17000.0, 0.0, 0.707, Slope::dB12)},
                           0.0, false});

        p.push_back(Preset{"Telephone",
                           {band(0, hp, 420.0, 0.0, 0.707, Slope::dB48),
                            band(1, lp, 3400.0, 0.0, 0.707, Slope::dB48)},
                           0.0, false});

        p.push_back(Preset{"Hum Killer",
                           {band(0, notch, 50.0, 0.0, 12.0),
                            band(1, notch, 100.0, 0.0, 12.0),
                            band(2, notch, 150.0, 0.0, 12.0)},
                           0.0, false});

        p.push_back(Preset{"Mastering Tilt",
                           {band(0, ls, 100.0, -2.0, 0.707),
                            band(1, hs, 10000.0, 2.0, 0.707)},
                           0.0, true});

        p.push_back(Preset{"Side Air (M/S)",
                           {band(0, hs, 9000.0, 3.5, 0.707, Slope::dB12, ChannelMode::SideOnly),
                            band(1, bell, 250.0, -2.5, 1.0, Slope::dB12, ChannelMode::MidOnly)},
                           0.0, false});

        return p;
    }();

    return presets;
}

}  // namespace eqm1
