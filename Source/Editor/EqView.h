#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>

#include "DSP/Curve.h"
#include "DSP/DspTypes.h"
#include "Processor/EqM1AudioProcessor.h"

namespace eqm1 {

// Main EQ display: spectrum analyser, cascade magnitude curve and draggable band nodes.
//
// Interaction model (mirrors Pro-Q):
//   drag node            frequency (x) and gain (y)
//   ctrl/cmd + drag      Q
//   wheel over node      Q
//   ctrl/cmd + wheel     vertical dB range of the view
//   double-click empty   create a band at that frequency
//   double-click node    enable / disable the band
//   right-click          context menu (type, channel, slope, solo, reset, remove)
//   arrows / delete / S  nudge, disable and solo the selected band
class EqView : public juce::Component, private juce::Timer {
public:
    static constexpr double kDisplayMinFreq = 20.0;
    static constexpr double kDisplayMaxFreq = 20000.0;

    explicit EqView(EqM1AudioProcessor& p);
    ~EqView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    enum class DragMode { None, FreqGain, QOnly };

    void timerCallback() override;

    juce::Rectangle<float> plotArea() const;
    float freqToX(double freqHz) const;
    double xToFreq(double x) const;
    float gainToY(double db) const;
    double yToGain(double y) const;

    int hitTestNode(juce::Point<float> pos) const;
    juce::Point<float> nodePosition(int band) const;
    int firstFreeBand() const;

    void selectBand(int band);
    void setSolo(int band, bool soloed);
    void addBandAt(juce::Point<float> pos);
    void nudgeSelectedBand(const juce::KeyPress&);

    double paramValue(const juce::String& id) const;
    void setParamValue(const juce::String& id, double realValue);

    void showNodeMenu(int band);
    void showBackgroundMenu(juce::Point<float> pos);
    void handleMenuResult(int band, juce::Point<float> pos, int result);

    void drawBackground(juce::Graphics&);
    void drawGrid(juce::Graphics&);
    void drawSpectrum(juce::Graphics&, const std::array<float, kSpectrumBins>& data,
                      juce::Colour colour, double rangeDb);
    void drawCurves(juce::Graphics&);
    void drawNodes(juce::Graphics&);
    void drawOverlayText(juce::Graphics&);

    EqM1AudioProcessor& proc;

    MagnitudeCurve curve;
    MagnitudeCurve selectedCurve;
    std::vector<double> curveDb;
    std::vector<double> selectedDb;

    std::array<float, kSpectrumBins> inDisplay{};
    std::array<float, kSpectrumBins> outDisplay{};
    bool haveInputSpectrum = false;
    bool haveOutputSpectrum = false;

    EngineInput lastSnapshot;
    bool haveSnapshot = false;

    int hoveredBand = -1;
    int draggedBand = -1;
    int lastSelected = -2;
    DragMode dragMode = DragMode::None;
    double dragStartFreq = 1000.0;
    double dragStartGain = 0.0;
    double dragStartQ = 0.707;
    juce::Point<float> dragOrigin;
    double viewRangeDb = 18.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqView)
};

}  // namespace eqm1
