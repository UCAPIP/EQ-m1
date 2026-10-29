#include "Editor/EqM1AudioProcessorEditor.h"

namespace eqm1 {

EqM1AudioProcessorEditor::EqM1AudioProcessorEditor(EqM1AudioProcessor& p)
    : AudioProcessorEditor(p), owner(p), eqView(p), controlBar(p) {
    juce::ignoreUnused(owner);

    addAndMakeVisible(eqView);
    addAndMakeVisible(controlBar);

    titleLabel.setText(juce::String(kPluginName) + "   parametric equalizer with solo monitoring",
                       juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.50f));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    setOpaque(true);
    setResizable(true, true);
    constrainer.setSizeLimits(820, 470, 2600, 1800);
    setConstrainer(&constrainer);
    setSize(1020, 620);
}

void EqM1AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0f1216));
}

void EqM1AudioProcessorEditor::resized() {
    auto area = getLocalBounds();
    titleLabel.setBounds(area.removeFromTop(26).reduced(12, 3));
    controlBar.setBounds(area.removeFromBottom(94));
    eqView.setBounds(area);
}

}  // namespace eqm1
