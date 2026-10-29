#pragma once

#include <JuceHeader.h>

#include "Editor/ControlBar.h"
#include "Editor/EqView.h"
#include "Processor/EqM1AudioProcessor.h"

namespace eqm1 {

class EqM1AudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit EqM1AudioProcessorEditor(EqM1AudioProcessor&);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    EqM1AudioProcessor& owner;
    EqView eqView;
    ControlBar controlBar;
    juce::Label titleLabel;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqM1AudioProcessorEditor)
};

}  // namespace eqm1
