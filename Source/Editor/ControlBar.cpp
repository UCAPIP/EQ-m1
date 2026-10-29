#include "Editor/ControlBar.h"

#include "Processor/Params.h"

namespace eqm1 {

namespace {

juce::Colour panelColour()    { return juce::Colour(0xff14171c); }
juce::Colour captionColour()  { return juce::Colours::white.withAlpha(0.50f); }
juce::Colour accentColour()   { return juce::Colour(0xff4fd8ee); }
juce::Colour soloColour()     { return juce::Colour(0xffffb347); }

void styleCombo(juce::ComboBox& combo) {
    combo.setTextWhenNothingSelected("");
    combo.setJustificationType(juce::Justification::centredLeft);
}

void styleSlider(juce::Slider& slider) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 18);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.85f));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentWhite);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentWhite);
    slider.setColour(juce::Slider::trackColourId, juce::Colours::white.withAlpha(0.12f));
    slider.setColour(juce::Slider::thumbColourId, accentColour());
}

void styleCaption(juce::Label& label) {
    label.setFont(juce::Font(juce::FontOptions(10.0f)));
    label.setColour(juce::Label::textColourId, captionColour());
    label.setJustificationType(juce::Justification::centredRight);
}

}  // namespace

ControlBar::ControlBar(EqM1AudioProcessor& p) : proc(p) {
    for (int i = 0; i < kMaxBands; ++i)
        bandCombo.addItem("Band " + juce::String(i + 1), i + 1);

    bandCombo.onChange = [this] {
        const int index = bandCombo.getSelectedId() - 1;
        if (index >= 0 && index != proc.getSelectedBand())
            proc.setSelectedBand(index);
    };

    typeCombo.addItemList(filterTypeNames(), 1);
    slopeCombo.addItemList(slopeNames(), 1);
    channelCombo.addItemList(channelModeNames(), 1);
    analyzerRangeCombo.addItemList(analyzerRangeNames(), 1);
    analyzerSpeedCombo.addItemList(analyzerSpeedNames(), 1);
    for (auto* c : {&typeCombo, &slopeCombo, &channelCombo, &analyzerRangeCombo, &analyzerSpeedCombo})
        styleCombo(*c);

    for (auto* s : {&freqSlider, &gainSlider, &qSlider, &soloWidthSlider, &outputGainSlider})
        styleSlider(*s);

    freqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 66, 18);
    for (auto* l : {&freqCaption, &gainCaption, &qCaption, &soloWidthCaption, &outGainCaption})
        styleCaption(*l);

    infoLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    infoLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
    infoLabel.setJustificationType(juce::Justification::centredRight);

    enableButton.setColour(juce::ToggleButton::tickColourId, accentColour());
    soloButton.setColour(juce::ToggleButton::tickColourId, soloColour());
    analyzerToggle.setColour(juce::ToggleButton::tickColourId, accentColour());
    autoGainToggle.setColour(juce::ToggleButton::tickColourId, accentColour());
    bypassToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffe2635f));

    soloButton.onClick = [this] {
        const int band = proc.getSelectedBand();
        if (band >= 0)
            setSolo(band, soloButton.getToggleState());
    };

    auto& apvts = proc.getApvts();
    analyzerAttachment = std::make_unique<ButtonAttachment>(apvts, ids::kAnalyzerOn, analyzerToggle);
    autoGainAttachment = std::make_unique<ButtonAttachment>(apvts, ids::kAutoGain, autoGainToggle);
    bypassAttachment = std::make_unique<ButtonAttachment>(apvts, ids::kBypass, bypassToggle);
    analyzerRangeAttachment = std::make_unique<ComboAttachment>(apvts, ids::kAnalyzerRange, analyzerRangeCombo);
    analyzerSpeedAttachment = std::make_unique<ComboAttachment>(apvts, ids::kAnalyzerSpeed, analyzerSpeedCombo);
    soloWidthAttachment = std::make_unique<SliderAttachment>(apvts, ids::kSoloWidth, soloWidthSlider);
    outputGainAttachment = std::make_unique<SliderAttachment>(apvts, ids::kOutGain, outputGainSlider);

    for (auto* c : {&bandCombo, &typeCombo, &slopeCombo, &channelCombo})
        addAndMakeVisible(c);
    for (auto* s : {&freqSlider, &gainSlider, &qSlider, &soloWidthSlider, &outputGainSlider})
        addAndMakeVisible(s);
    for (auto* l : {&freqCaption, &gainCaption, &qCaption, &soloWidthCaption, &outGainCaption, &infoLabel})
        addAndMakeVisible(l);
    for (auto* b : {&enableButton, &soloButton, &analyzerToggle, &autoGainToggle, &bypassToggle})
        addAndMakeVisible(b);

    rebuildBandAttachments();
    startTimerHz(20);
}

ControlBar::~ControlBar() { stopTimer(); }

void ControlBar::setSolo(int band, bool soloed) {
    auto& apvts = proc.getApvts();
    for (int i = 0; i < kMaxBands; ++i)
        setParamRealValue(apvts, bandParamId(i, ids::kSolo), (soloed && i == band) ? 1.0 : 0.0);

    if (soloed && juce::isPositiveAndBelow(band, kMaxBands))
        setParamRealValue(apvts, bandParamId(band, ids::kOn), 1.0);

    refreshBandWidgets();
}

void ControlBar::rebuildBandAttachments() {
    attachedBand = proc.getSelectedBand();
    const bool valid = juce::isPositiveAndBelow(attachedBand, kMaxBands);

    typeAttachment.reset();
    slopeAttachment.reset();
    channelAttachment.reset();
    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    enableAttachment.reset();

    bandCombo.setSelectedItemIndex(valid ? attachedBand : 0, juce::dontSendNotification);

    if (!valid) {
        for (auto* c : {&typeCombo, &slopeCombo, &channelCombo})
            c->setEnabled(false);
        for (auto* s : {&freqSlider, &gainSlider, &qSlider})
            s->setEnabled(false);
        enableButton.setEnabled(false);
        soloButton.setEnabled(false);
        return;
    }

    auto& apvts = proc.getApvts();
    typeAttachment = std::make_unique<ComboAttachment>(apvts, bandParamId(attachedBand, ids::kType), typeCombo);
    slopeAttachment = std::make_unique<ComboAttachment>(apvts, bandParamId(attachedBand, ids::kSlope), slopeCombo);
    channelAttachment = std::make_unique<ComboAttachment>(apvts, bandParamId(attachedBand, ids::kChannel), channelCombo);
    freqAttachment = std::make_unique<SliderAttachment>(apvts, bandParamId(attachedBand, ids::kFreq), freqSlider);
    gainAttachment = std::make_unique<SliderAttachment>(apvts, bandParamId(attachedBand, ids::kGain), gainSlider);
    qAttachment = std::make_unique<SliderAttachment>(apvts, bandParamId(attachedBand, ids::kQ), qSlider);
    enableAttachment = std::make_unique<ButtonAttachment>(apvts, bandParamId(attachedBand, ids::kOn), enableButton);

    enableButton.setEnabled(true);
    soloButton.setEnabled(true);
    refreshBandWidgets();
}

void ControlBar::refreshBandWidgets() {
    if (!juce::isPositiveAndBelow(attachedBand, kMaxBands)) {
        infoLabel.setText("no band selected - double-click the graph to add one",
                          juce::dontSendNotification);
        return;
    }

    auto& apvts = proc.getApvts();
    const auto type = static_cast<FilterType>(
        juce::jlimit(0, static_cast<int>(FilterType::count) - 1,
                     static_cast<int>(std::lround(getParamRealValue(apvts, bandParamId(attachedBand, ids::kType))))));
    const auto slope = static_cast<Slope>(
        juce::jlimit(0, static_cast<int>(Slope::count) - 1,
                     static_cast<int>(std::lround(getParamRealValue(apvts, bandParamId(attachedBand, ids::kSlope))))));

    const bool gainType = hasGain(type);
    const bool passType = (type == FilterType::LowPass || type == FilterType::HighPass);
    const bool qUsed = !passType;

    gainSlider.setEnabled(gainType);
    gainCaption.setEnabled(gainType);
    qSlider.setEnabled(qUsed);
    qCaption.setEnabled(qUsed);
    slopeCombo.setEnabled(passType);

    const bool soloed = getParamRealValue(apvts, bandParamId(attachedBand, ids::kSolo)) > 0.5;
    soloButton.setToggleState(soloed, juce::dontSendNotification);
    soloButton.setColour(juce::TextButton::textColourOffId,
                         soloed ? soloColour() : juce::Colours::white.withAlpha(0.75f));

    const double freq = getParamRealValue(apvts, bandParamId(attachedBand, ids::kFreq));
    const double gain = getParamRealValue(apvts, bandParamId(attachedBand, ids::kGain));
    const double q = getParamRealValue(apvts, bandParamId(attachedBand, ids::kQ));
    const auto channel = static_cast<ChannelMode>(
        juce::jlimit(0, static_cast<int>(ChannelMode::count) - 1,
                     static_cast<int>(std::lround(getParamRealValue(apvts, bandParamId(attachedBand, ids::kChannel))))));

    juce::String info = "Band " + juce::String(attachedBand + 1) + "  |  " + filterTypeName(type)
                      + "  |  " + freqText(static_cast<float>(freq)) + "  |  "
                      + (gainType ? gainText(static_cast<float>(gain)) : slopeName(slope)) + "  |  "
                      + (qUsed ? "Q " + qText(static_cast<float>(q)) : "")
                      + "  |  " + channelModeName(channel);

    if (getParamRealValue(apvts, ids::kAutoGain) > 0.5)
        info += "  |  auto gain";

    infoLabel.setText(info, juce::dontSendNotification);
}

void ControlBar::timerCallback() {
    if (proc.getSelectedBand() != attachedBand)
        rebuildBandAttachments();
    else
        refreshBandWidgets();
}

void ControlBar::layoutRow(juce::Rectangle<int> row,
                           const std::vector<std::pair<juce::Component*, float>>& items) {
    float total = 0.0f;
    for (const auto& item : items)
        total += item.second;

    if (total <= 0.0f)
        return;

    for (const auto& item : items) {
        const int w = juce::roundToInt(static_cast<float>(row.getWidth()) * item.second / total);
        item.first->setBounds(row.removeFromLeft(w).reduced(3, 0));
    }
}

void ControlBar::resized() {
    auto area = getLocalBounds().reduced(8, 6);
    const int rowHeight = area.getHeight() / 2;

    layoutRow(area.removeFromTop(rowHeight - 3), {
        {&bandCombo, 0.80f}, {&typeCombo, 0.95f},
        {&freqCaption, 0.34f}, {&freqSlider, 1.25f},
        {&gainCaption, 0.34f}, {&gainSlider, 1.20f},
        {&qCaption, 0.22f}, {&qSlider, 1.05f},
        {&slopeCombo, 0.80f}, {&channelCombo, 0.85f},
        {&enableButton, 0.45f}, {&soloButton, 0.52f}});

    layoutRow(area, {
        {&analyzerToggle, 0.68f}, {&analyzerRangeCombo, 0.60f}, {&analyzerSpeedCombo, 0.66f},
        {&soloWidthCaption, 0.60f}, {&soloWidthSlider, 1.00f},
        {&outGainCaption, 0.44f}, {&outputGainSlider, 1.00f},
        {&autoGainToggle, 0.72f}, {&bypassToggle, 0.55f}, {&infoLabel, 1.90f}});
}

void ControlBar::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();
    g.setColour(panelColour());
    g.fillRect(b);
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.drawHorizontalLine(0, b.getX(), b.getRight());
}

}  // namespace eqm1
