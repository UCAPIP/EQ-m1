#pragma once

#include <JuceHeader.h>

#include <memory>
#include <utility>
#include <vector>

#include "Processor/EqM1AudioProcessor.h"

namespace eqm1 {

// Bottom strip: controls for the selected band plus the global section.
class ControlBar : public juce::Component, private juce::Timer {
public:
    explicit ControlBar(EqM1AudioProcessor& p);
    ~ControlBar() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void rebuildBandAttachments();
    void refreshBandWidgets();
    void setSolo(int band, bool soloed);
    void layoutRow(juce::Rectangle<int> row,
                   const std::vector<std::pair<juce::Component*, float>>& items);

    EqM1AudioProcessor& proc;
    int attachedBand = -2;

    juce::ComboBox bandCombo;
    juce::ComboBox typeCombo, slopeCombo, channelCombo;
    juce::Slider freqSlider, gainSlider, qSlider;
    juce::ToggleButton enableButton{"On"};
    juce::ToggleButton soloButton{"Solo"};
    juce::Label freqCaption{"freqCap", "Freq"}, gainCaption{"gainCap", "Gain"}, qCaption{"qCap", "Q"};

    std::unique_ptr<ComboAttachment> typeAttachment, slopeAttachment, channelAttachment;
    std::unique_ptr<SliderAttachment> freqAttachment, gainAttachment, qAttachment;
    std::unique_ptr<ButtonAttachment> enableAttachment;

    juce::ToggleButton analyzerToggle{"Analyzer"}, autoGainToggle{"Auto gain"}, bypassToggle{"Bypass"};
    juce::ComboBox analyzerRangeCombo, analyzerSpeedCombo;
    juce::Slider soloWidthSlider, outputGainSlider;
    juce::Label soloWidthCaption{"swCap", "Solo width"}, outGainCaption{"ogCap", "Output"};
    juce::Label infoLabel;

    std::unique_ptr<ButtonAttachment> analyzerAttachment, autoGainAttachment, bypassAttachment;
    std::unique_ptr<ComboAttachment> analyzerRangeAttachment, analyzerSpeedAttachment;
    std::unique_ptr<SliderAttachment> soloWidthAttachment, outputGainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlBar)
};

}  // namespace eqm1
