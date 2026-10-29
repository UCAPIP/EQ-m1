#include "Editor/EqView.h"

#include <cmath>

#include "Processor/Params.h"

namespace eqm1 {

namespace {

juce::Colour colourBackgroundTop()    { return juce::Colour(0xff0f1216); }
juce::Colour colourBackgroundBottom() { return juce::Colour(0xff1c2027); }
juce::Colour colourGridMinor()        { return juce::Colours::white.withAlpha(0.045f); }
juce::Colour colourGridMajor()        { return juce::Colours::white.withAlpha(0.10f); }
juce::Colour colourZeroLine()         { return juce::Colours::white.withAlpha(0.24f); }
juce::Colour colourLabelText()        { return juce::Colours::white.withAlpha(0.42f); }
juce::Colour colourCurveLine()        { return juce::Colour(0xff8ef0ff); }
juce::Colour colourCurveFill()        { return juce::Colour(0xff4fd8ee).withAlpha(0.15f); }
juce::Colour colourBandCurve()        { return juce::Colours::white.withAlpha(0.30f); }
juce::Colour colourInputSpectrum()    { return juce::Colour(0xff5b8fd6).withAlpha(0.28f); }
juce::Colour colourOutputSpectrum()   { return juce::Colour(0xffe0a83c).withAlpha(0.34f); }
juce::Colour colourNodeEnabled()      { return juce::Colour(0xff4fd8ee); }
juce::Colour colourNodeDisabled()     { return juce::Colour(0xff6d737b); }
juce::Colour colourNodeSolo()         { return juce::Colour(0xffffb347); }

constexpr int kNodeRadius = 8;
constexpr int kSelectedNodeRadius = 11;
constexpr int kHitRadius = 14;
constexpr int kCurvePoints = 720;
constexpr int kFreqLabelStrip = 18;
constexpr double kMaxCurveDb = 220.0;

juce::Font fontSmall() { return juce::Font(juce::FontOptions(10.0f)); }
juce::Font fontNode()  { return juce::Font(juce::FontOptions(9.0f).withStyle("Bold")); }
juce::Font fontHint()  { return juce::Font(juce::FontOptions(11.0f)); }

const double kViewRangeOptions[] = {6.0, 12.0, 18.0, 24.0, 30.0};
const double kSoloWidthOptions[] = {0.25, 0.5, 1.0, 2.0, 4.0};

}  // namespace

EqView::EqView(EqM1AudioProcessor& p) : proc(p) {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);

    curveDb.assign(kCurvePoints, 0.0);
    selectedDb.assign(kCurvePoints, 0.0);
    inDisplay.fill(static_cast<float>(kSpectrumFloorDb));
    outDisplay.fill(static_cast<float>(kSpectrumFloorDb));

    lastSnapshot = proc.snapshot();
    haveSnapshot = true;

    startTimerHz(30);
}

EqView::~EqView() { stopTimer(); }

// --- geometry --------------------------------------------------------------
juce::Rectangle<float> EqView::plotArea() const {
    return getLocalBounds().reduced(10, 8).withTrimmedBottom(kFreqLabelStrip).toFloat();
}

float EqView::freqToX(double freqHz) const {
    const auto plot = plotArea();
    const double lo = std::log(kDisplayMinFreq);
    const double hi = std::log(kDisplayMaxFreq);
    const double t = (std::log(juce::jlimit(kDisplayMinFreq, kDisplayMaxFreq, freqHz)) - lo) / (hi - lo);
    return plot.getX() + static_cast<float>(t) * plot.getWidth();
}

double EqView::xToFreq(double x) const {
    const auto plot = plotArea();
    const double t = (x - plot.getX()) / std::max(1.0f, plot.getWidth());
    const double lo = std::log(kDisplayMinFreq);
    const double hi = std::log(kDisplayMaxFreq);
    return std::exp(lo + (hi - lo) * juce::jlimit(0.0, 1.0, t));
}

float EqView::gainToY(double db) const {
    const auto plot = plotArea();
    const double clamped = juce::jlimit(-kMaxCurveDb, kMaxCurveDb, db);
    const double t = 0.5 - (clamped / (2.0 * viewRangeDb));
    return plot.getY() + static_cast<float>(t) * plot.getHeight();
}

double EqView::yToGain(double y) const {
    const auto plot = plotArea();
    const double t = (y - plot.getY()) / std::max(1.0f, plot.getHeight());
    return (0.5 - juce::jlimit(0.0, 1.0, t)) * 2.0 * viewRangeDb;
}

juce::Point<float> EqView::nodePosition(int band) const {
    if (!juce::isPositiveAndBelow(band, kMaxBands))
        return {};
    const BandParams& bp = lastSnapshot.bands[band];
    const double db = hasGain(bp.type) ? bp.gainDb : 0.0;
    return {freqToX(bp.frequency), gainToY(db)};
}

int EqView::hitTestNode(juce::Point<float> pos) const {
    int best = -1;
    float bestDist = static_cast<float>(kHitRadius);
    for (int i = 0; i < kMaxBands; ++i) {
        const float d = pos.getDistanceFrom(nodePosition(i));
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

int EqView::firstFreeBand() const {
    for (int i = 0; i < kMaxBands; ++i)
        if (!lastSnapshot.bands[i].enabled && !lastSnapshot.bands[i].solo)
            return i;
    return -1;
}

// --- parameters ------------------------------------------------------------
double EqView::paramValue(const juce::String& id) const {
    return getParamRealValue(proc.getApvts(), id);
}

void EqView::setParamValue(const juce::String& id, double realValue) {
    setParamRealValue(proc.getApvts(), id, realValue);
}

void EqView::selectBand(int band) {
    proc.setSelectedBand(band);
    lastSelected = band;
    grabKeyboardFocus();
    repaint();
}

void EqView::setSolo(int band, bool soloed) {
    for (int i = 0; i < kMaxBands; ++i)
        setParamValue(bandParamId(i, ids::kSolo), (soloed && i == band) ? 1.0 : 0.0);

    // A soloed band must be part of the chain, otherwise there is nothing to listen to.
    if (soloed && juce::isPositiveAndBelow(band, kMaxBands))
        setParamValue(bandParamId(band, ids::kOn), 1.0);
}

void EqView::addBandAt(juce::Point<float> pos) {
    const int band = firstFreeBand();
    if (band < 0)
        return;

    setParamValue(bandParamId(band, ids::kType), static_cast<double>(FilterType::Bell));
    setParamValue(bandParamId(band, ids::kChannel), 0.0);
    setParamValue(bandParamId(band, ids::kFreq), xToFreq(pos.x));
    setParamValue(bandParamId(band, ids::kGain), juce::jlimit(-viewRangeDb, viewRangeDb, yToGain(pos.y)));
    setParamValue(bandParamId(band, ids::kQ), 1.0);
    setParamValue(bandParamId(band, ids::kSolo), 0.0);
    setParamValue(bandParamId(band, ids::kOn), 1.0);
    selectBand(band);
}

// --- mouse -----------------------------------------------------------------
void EqView::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();

    if (e.mods.isPopupMenu()) {
        const int band = hitTestNode(e.position);
        if (band >= 0) {
            selectBand(band);
            showNodeMenu(band);
        } else {
            showBackgroundMenu(e.position);
        }
        return;
    }

    const int band = hitTestNode(e.position);
    if (band < 0)
        return;

    selectBand(band);

    // Dragging a node is an explicit intent to use that band, so an idle (grey) node is
    // switched on: otherwise the user moves a point and neither the curve nor the audio
    // reacts, which reads as a broken plugin.
    if (paramValue(bandParamId(band, ids::kOn)) < 0.5)
        setParamValue(bandParamId(band, ids::kOn), 1.0);

    draggedBand = band;
    dragMode = (e.mods.isCtrlDown() || e.mods.isAltDown() || e.mods.isCommandDown())
                   ? DragMode::QOnly
                   : DragMode::FreqGain;
    dragOrigin = e.position;
    dragStartFreq = paramValue(bandParamId(band, ids::kFreq));
    dragStartGain = paramValue(bandParamId(band, ids::kGain));
    dragStartQ = paramValue(bandParamId(band, ids::kQ));
}

void EqView::mouseDrag(const juce::MouseEvent& e) {
    if (draggedBand < 0)
        return;

    const auto plot = plotArea();
    const double dx = e.position.x - dragOrigin.x;
    const double dy = e.position.y - dragOrigin.y;
    const double fine = e.mods.isShiftDown() ? 0.25 : 1.0;
    const double octavesPerPixel =
        std::log2(kDisplayMaxFreq / kDisplayMinFreq) / static_cast<double>(plot.getWidth());

    const BandParams& bp = lastSnapshot.bands[draggedBand];
    const bool qDrag = dragMode == DragMode::QOnly || !hasGain(bp.type);

    if (qDrag && bp.usesQ()) {
        setParamValue(bandParamId(draggedBand, ids::kQ),
                      dragStartQ * std::pow(2.0, -dy * octavesPerPixel * fine * 2.0));
    }

    if (dragMode == DragMode::FreqGain) {
        setParamValue(bandParamId(draggedBand, ids::kFreq),
                      dragStartFreq * std::pow(2.0, dx * octavesPerPixel * fine));
        if (hasGain(bp.type)) {
            const double dbPerPixel = (2.0 * viewRangeDb) / static_cast<double>(plot.getHeight());
            setParamValue(bandParamId(draggedBand, ids::kGain), dragStartGain - dy * dbPerPixel * fine);
        }
    }
}

void EqView::mouseUp(const juce::MouseEvent&) {
    draggedBand = -1;
    dragMode = DragMode::None;
}

void EqView::mouseMove(const juce::MouseEvent& e) {
    const int band = hitTestNode(e.position);
    if (band != hoveredBand) {
        hoveredBand = band;
        repaint();
    }
    setMouseCursor(band >= 0 ? juce::MouseCursor::PointingHandCursor
                             : juce::MouseCursor::CrosshairCursor);
}

void EqView::mouseExit(const juce::MouseEvent&) {
    if (hoveredBand != -1) {
        hoveredBand = -1;
        repaint();
    }
}

void EqView::mouseDoubleClick(const juce::MouseEvent& e) {
    const int band = hitTestNode(e.position);
    if (band >= 0) {
        const bool on = paramValue(bandParamId(band, ids::kOn)) > 0.5;
        if (on)
            setParamValue(bandParamId(band, ids::kSolo), 0.0);
        setParamValue(bandParamId(band, ids::kOn), on ? 0.0 : 1.0);
        selectBand(band);
        return;
    }
    addBandAt(e.position);
}

void EqView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    const double delta = std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
    if (std::abs(delta) < 1.0e-6)
        return;

    const int band = hitTestNode(e.position);
    if (band >= 0) {
        const BandParams& bp = lastSnapshot.bands[band];
        if (bp.usesQ()) {
            selectBand(band);
            setParamValue(bandParamId(band, ids::kQ),
                          paramValue(bandParamId(band, ids::kQ)) * std::pow(2.0, delta * 0.35));
        }
        return;
    }

    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) {
        const int current = [&] {
            int best = 2;
            double bestDiff = 1.0e9;
            for (int i = 0; i < juce::numElementsInArray(kViewRangeOptions); ++i) {
                const double d = std::abs(kViewRangeOptions[i] - viewRangeDb);
                if (d < bestDiff) { bestDiff = d; best = i; }
            }
            return best;
        }();
        const int next = juce::jlimit(0, juce::numElementsInArray(kViewRangeOptions) - 1,
                                      current + (delta > 0.0 ? -1 : 1));
        if (next != current) {
            viewRangeDb = kViewRangeOptions[next];
            repaint();
        }
    }
}

bool EqView::keyPressed(const juce::KeyPress& key) {
    const int band = proc.getSelectedBand();
    const int code = key.getKeyCode();

    if (code == juce::KeyPress::escapeKey) {
        selectBand(-1);
        return true;
    }

    if (!juce::isPositiveAndBelow(band, kMaxBands))
        return false;

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) {
        setParamValue(bandParamId(band, ids::kSolo), 0.0);
        setParamValue(bandParamId(band, ids::kOn), 0.0);
        return true;
    }

    if (code == 'S' || code == 's' || code == juce::KeyPress::F2Key) {
        setSolo(band, paramValue(bandParamId(band, ids::kSolo)) < 0.5);
        return true;
    }

    nudgeSelectedBand(key);
    return code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
        || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
}

void EqView::nudgeSelectedBand(const juce::KeyPress& key) {
    const int band = proc.getSelectedBand();
    if (!juce::isPositiveAndBelow(band, kMaxBands))
        return;

    // Same rule as dragging: touching the band's values activates it.
    if (paramValue(bandParamId(band, ids::kOn)) < 0.5)
        setParamValue(bandParamId(band, ids::kOn), 1.0);

    const int code = key.getKeyCode();
    const bool fine = key.getModifiers().isShiftDown();

    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
        const double semitones = fine ? 0.1 : 1.0;
        const double factor = std::pow(2.0, semitones / 12.0);
        const double f = paramValue(bandParamId(band, ids::kFreq));
        setParamValue(bandParamId(band, ids::kFreq),
                      code == juce::KeyPress::rightKey ? f * factor : f / factor);
        return;
    }

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
        const BandParams& bp = lastSnapshot.bands[band];
        const double step = fine ? 0.1 : 0.5;
        if (bp.usesQ() && !hasGain(bp.type)) {
            const double q = paramValue(bandParamId(band, ids::kQ));
            setParamValue(bandParamId(band, ids::kQ),
                          code == juce::KeyPress::upKey ? q * 1.1 : q / 1.1);
            return;
        }
        if (hasGain(bp.type)) {
            const double g = paramValue(bandParamId(band, ids::kGain));
            setParamValue(bandParamId(band, ids::kGain),
                          code == juce::KeyPress::upKey ? g + step : g - step);
        }
    }
}

// --- context menus ---------------------------------------------------------
void EqView::showNodeMenu(int band) {
    if (!juce::isPositiveAndBelow(band, kMaxBands))
        return;

    const BandParams& bp = lastSnapshot.bands[band];

    juce::PopupMenu menu;
    menu.addItem(1, bp.enabled ? "Disable band " + juce::String(band + 1)
                               : "Enable band " + juce::String(band + 1));
    menu.addItem(2, bp.solo ? "Stop solo monitoring" : "Solo this range (listen to it alone)");
    menu.addSeparator();

    juce::PopupMenu types;
    for (int i = 0; i < static_cast<int>(FilterType::count); ++i)
        types.addItem(100 + i, filterTypeName(static_cast<FilterType>(i)), true,
                      static_cast<int>(bp.type) == i);
    menu.addSubMenu("Filter type", types);

    juce::PopupMenu channels;
    for (int i = 0; i < static_cast<int>(ChannelMode::count); ++i)
        channels.addItem(200 + i, channelModeName(static_cast<ChannelMode>(i)), true,
                         static_cast<int>(bp.channel) == i);
    menu.addSubMenu("Channel", channels);

    if (bp.type == FilterType::LowPass || bp.type == FilterType::HighPass) {
        juce::PopupMenu slopes;
        for (int i = 0; i < static_cast<int>(Slope::count); ++i)
            slopes.addItem(300 + i, slopeName(static_cast<Slope>(i)), true,
                           static_cast<int>(bp.slope) == i);
        menu.addSubMenu("Slope", slopes);
    }

    menu.addSeparator();
    menu.addItem(3, "Reset gain");
    menu.addItem(4, "Reset Q");
    menu.addItem(5, "Reset frequency");
    menu.addSeparator();
    menu.addItem(6, "Remove band");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(180),
                       [safe = juce::Component::SafePointer(this), band](int result) {
                           if (safe == nullptr || result == 0)
                               return;
                           if (auto* self = dynamic_cast<EqView*>(safe.getComponent()))
                               self->handleMenuResult(band, {}, result);
                       });
}

void EqView::showBackgroundMenu(juce::Point<float> pos) {
    juce::PopupMenu menu;
    const bool hasFree = firstFreeBand() >= 0;
    menu.addItem(400, "Add band here", hasFree, false);
    menu.addSeparator();

    menu.addItem(430, juce::String(paramValue(ids::kAnalyzerOn) > 0.5 ? "Hide analyzer"
                                                                     : "Show analyzer"));

    const int rangeIndex = juce::jlimit(0, 3, static_cast<int>(std::lround(paramValue(ids::kAnalyzerRange))));
    const int speedIndex = juce::jlimit(0, 3, static_cast<int>(std::lround(paramValue(ids::kAnalyzerSpeed))));

    juce::PopupMenu ranges;
    const auto rangeNames = analyzerRangeNames();
    for (int i = 0; i < rangeNames.size(); ++i)
        ranges.addItem(410 + i, rangeNames[i], true, rangeIndex == i);
    menu.addSubMenu("Analyzer range", ranges);

    juce::PopupMenu speeds;
    const auto speedNames = analyzerSpeedNames();
    for (int i = 0; i < speedNames.size(); ++i)
        speeds.addItem(420 + i, speedNames[i], true, speedIndex == i);
    menu.addSubMenu("Analyzer speed", speeds);

    juce::PopupMenu widths;
    const double currentWidth = paramValue(ids::kSoloWidth);
    for (int i = 0; i < juce::numElementsInArray(kSoloWidthOptions); ++i) {
        const double w = kSoloWidthOptions[i];
        widths.addItem(440 + i, juce::String(w, 2) + (i == 2 ? " octave" : " octaves"), true,
                       std::abs(w - currentWidth) < 0.05);
    }
    menu.addSubMenu("Solo width", widths);

    juce::PopupMenu viewRanges;
    for (int i = 0; i < juce::numElementsInArray(kViewRangeOptions); ++i)
        viewRanges.addItem(460 + i, "+/- " + juce::String(kViewRangeOptions[i], 0) + " dB", true,
                           std::abs(kViewRangeOptions[i] - viewRangeDb) < 0.01);
    menu.addSubMenu("Vertical range", viewRanges);

    menu.addSeparator();
    menu.addItem(450, "Disable all bands");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(180),
                       [safe = juce::Component::SafePointer(this), pos](int result) {
                           if (safe == nullptr || result == 0)
                               return;
                           if (auto* self = dynamic_cast<EqView*>(safe.getComponent()))
                               self->handleMenuResult(-1, pos, result);
                       });
}

void EqView::handleMenuResult(int band, juce::Point<float> pos, int result) {
    const bool hasBand = juce::isPositiveAndBelow(band, kMaxBands);

    if (result == 400) {
        addBandAt(pos);
        return;
    }
    if (result == 430) {
        setParamValue(ids::kAnalyzerOn, paramValue(ids::kAnalyzerOn) > 0.5 ? 0.0 : 1.0);
        return;
    }
    if (result >= 410 && result < 414) {
        setParamValue(ids::kAnalyzerRange, result - 410);
        return;
    }
    if (result >= 420 && result < 424) {
        setParamValue(ids::kAnalyzerSpeed, result - 420);
        return;
    }
    if (result >= 440 && result < 440 + juce::numElementsInArray(kSoloWidthOptions)) {
        setParamValue(ids::kSoloWidth, kSoloWidthOptions[result - 440]);
        return;
    }
    if (result >= 460 && result < 460 + juce::numElementsInArray(kViewRangeOptions)) {
        viewRangeDb = kViewRangeOptions[result - 460];
        repaint();
        return;
    }
    if (result == 450) {
        for (int i = 0; i < kMaxBands; ++i) {
            setParamValue(bandParamId(i, ids::kOn), 0.0);
            setParamValue(bandParamId(i, ids::kSolo), 0.0);
        }
        return;
    }

    if (!hasBand)
        return;

    if (result == 1) {
        const bool on = paramValue(bandParamId(band, ids::kOn)) > 0.5;
        if (on)
            setParamValue(bandParamId(band, ids::kSolo), 0.0);
        setParamValue(bandParamId(band, ids::kOn), on ? 0.0 : 1.0);
    } else if (result == 2) {
        setSolo(band, paramValue(bandParamId(band, ids::kSolo)) < 0.5);
    } else if (result == 3) {
        setParamValue(bandParamId(band, ids::kGain), 0.0);
    } else if (result == 4) {
        setParamValue(bandParamId(band, ids::kQ), 0.707);
    } else if (result == 5) {
        setParamValue(bandParamId(band, ids::kFreq), defaultBandFrequency(band));
    } else if (result == 6) {
        setParamValue(bandParamId(band, ids::kSolo), 0.0);
        setParamValue(bandParamId(band, ids::kOn), 0.0);
    } else if (result >= 300 && result < 300 + static_cast<int>(Slope::count)) {
        setParamValue(bandParamId(band, ids::kSlope), result - 300);
    } else if (result >= 200 && result < 200 + static_cast<int>(ChannelMode::count)) {
        setParamValue(bandParamId(band, ids::kChannel), result - 200);
    } else if (result >= 100 && result < 100 + static_cast<int>(FilterType::count)) {
        setParamValue(bandParamId(band, ids::kType), result - 100);
    }
}

// --- timer -----------------------------------------------------------------
void EqView::timerCallback() {
    bool changed = false;

    const int speedIndex = juce::jlimit(0, 3, static_cast<int>(std::lround(paramValue(ids::kAnalyzerSpeed))));
    const bool frozen = speedIndex == 0;
    const double decay = analyzerDecayPerFrame(speedIndex);

    SpectrumAnalyser::Frame frame;
    while (proc.readInputSpectrum(frame)) {
        haveInputSpectrum = true;
        if (!frozen) {
            for (int i = 0; i < kSpectrumBins; ++i)
                inDisplay[static_cast<size_t>(i)] =
                    juce::jmax(frame[static_cast<size_t>(i)],
                               static_cast<float>(inDisplay[static_cast<size_t>(i)] - decay));
            changed = true;
        }
    }
    while (proc.readOutputSpectrum(frame)) {
        haveOutputSpectrum = true;
        if (!frozen) {
            for (int i = 0; i < kSpectrumBins; ++i)
                outDisplay[static_cast<size_t>(i)] =
                    juce::jmax(frame[static_cast<size_t>(i)],
                               static_cast<float>(outDisplay[static_cast<size_t>(i)] - decay));
            changed = true;
        }
    }

    if (!frozen) {
        for (int i = 0; i < kSpectrumBins; ++i) {
            auto& in = inDisplay[static_cast<size_t>(i)];
            auto& out = outDisplay[static_cast<size_t>(i)];
            if (in > kSpectrumFloorDb) { in = juce::jmax(static_cast<float>(kSpectrumFloorDb), static_cast<float>(in - decay)); changed = true; }
            if (out > kSpectrumFloorDb) { out = juce::jmax(static_cast<float>(kSpectrumFloorDb), static_cast<float>(out - decay)); changed = true; }
        }
    }

    const EngineInput snapshot = proc.snapshot();
    if (!haveSnapshot || snapshot != lastSnapshot) {
        lastSnapshot = snapshot;
        haveSnapshot = true;
        changed = true;
    }

    const int selected = proc.getSelectedBand();
    if (selected != lastSelected) {
        lastSelected = selected;
        changed = true;
    }

    if (changed)
        repaint();
}

// --- painting --------------------------------------------------------------
void EqView::resized() { repaint(); }

void EqView::drawBackground(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(colourBackgroundTop(), b.getX(), b.getY(),
                                           colourBackgroundBottom(), b.getX(), b.getBottom(), false));
    g.fillRect(b);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawRect(b, 1.0f);
}

void EqView::drawGrid(juce::Graphics& g) {
    const auto plot = plotArea();

    g.setFont(fontSmall());

    // dB lines
    const double step = viewRangeDb > 20.0 ? 12.0 : 6.0;
    for (double db = -std::floor(viewRangeDb / step) * step; db <= viewRangeDb; db += step) {
        const float y = gainToY(db);
        if (y < plot.getY() - 1.0f || y > plot.getBottom() + 1.0f)
            continue;

        const bool zero = std::abs(db) < 1.0e-6;
        g.setColour(zero ? colourZeroLine() : colourGridMinor());
        g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());

        g.setColour(zero ? colourLabelText().brighter() : colourLabelText());
        g.drawText(db > 0.0 ? "+" + juce::String(db, 0) : juce::String(db, 0),
                   juce::Rectangle<float>(plot.getX() + 4.0f, y - 13.0f, 40.0f, 12.0f),
                   juce::Justification::centredLeft);
    }

    // frequency lines
    for (int decade = 1; decade <= 20000; decade *= 10) {
        for (int m = 1; m <= 9; ++m) {
            const double f = static_cast<double>(decade) * m;
            if (f < kDisplayMinFreq || f > kDisplayMaxFreq)
                continue;

            const float x = freqToX(f);
            const bool major = (m == 1);
            g.setColour(major ? colourGridMajor() : colourGridMinor());
            g.drawVerticalLine(static_cast<int>(x), plot.getY(), plot.getBottom() + 4.0f);

            if (major || m == 5) {
                const juce::String label = f >= 1000.0 ? juce::String(f / 1000.0, f >= 10000.0 ? 0 : 1) + "k"
                                                       : juce::String(static_cast<int>(f));
                g.setColour(colourLabelText());
                g.setFont(fontSmall());
                g.drawText(label, juce::Rectangle<float>(x - 22.0f, plot.getBottom() + 4.0f, 44.0f,
                                                         static_cast<float>(kFreqLabelStrip)),
                           juce::Justification::centredTop);
            }
        }
    }
}

void EqView::drawSpectrum(juce::Graphics& g, const std::array<float, kSpectrumBins>& data,
                          juce::Colour colour, double rangeDb) {
    const auto plot = plotArea();
    const double top = 0.1 * rangeDb;
    const double bottom = -0.9 * rangeDb;
    const double span = top - bottom;

    juce::Path path;
    bool started = false;

    for (int i = 0; i < kSpectrumBins; ++i) {
        const double f = frequencyForDisplayBin(i, kSpectrumBins, kSpectrumMinFreq, kSpectrumMaxFreq);
        const float x = freqToX(f);
        const double norm = (top - data[static_cast<size_t>(i)]) / span;
        const float y = juce::jlimit(plot.getY(), plot.getBottom(),
                                     plot.getY() + static_cast<float>(norm) * plot.getHeight());

        if (!started) {
            path.startNewSubPath(x, plot.getBottom());
            path.lineTo(x, y);
            started = true;
        } else {
            path.lineTo(x, y);
        }
    }

    if (!started)
        return;

    path.lineTo(plot.getRight(), plot.getBottom());
    path.closeSubPath();

    g.setColour(colour);
    g.fillPath(path);
    g.setColour(colour.brighter().withAlpha(0.55f));
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

void EqView::drawCurves(juce::Graphics& g) {
    const auto plot = plotArea();
    const double fs = proc.getAnalysisSampleRate();

    curve.update(lastSnapshot.bands, kMaxBands, fs);
    curve.render(curveDb.data(), kCurvePoints, kDisplayMinFreq, kDisplayMaxFreq);

    const int selected = proc.getSelectedBand();
    if (juce::isPositiveAndBelow(selected, kMaxBands) && lastSnapshot.bands[selected].enabled) {
        BandParams single = lastSnapshot.bands[selected];
        single.enabled = true;
        single.solo = false;
        selectedCurve.update(&single, 1, fs);
        selectedCurve.render(selectedDb.data(), kCurvePoints, kDisplayMinFreq, kDisplayMaxFreq);

        juce::Path bandPath;
        for (int i = 0; i < kCurvePoints; ++i) {
            const float x = plot.getX() + plot.getWidth() * static_cast<float>(i)
                                        / static_cast<float>(kCurvePoints - 1);
            const float y = gainToY(selectedDb[static_cast<size_t>(i)]);
            if (i == 0)
                bandPath.startNewSubPath(x, y);
            else
                bandPath.lineTo(x, y);
        }
        g.setColour(colourBandCurve());
        g.strokePath(bandPath, juce::PathStrokeType(1.0f));
    }

    juce::Path main;
    for (int i = 0; i < kCurvePoints; ++i) {
        const float x = plot.getX() + plot.getWidth() * static_cast<float>(i)
                                    / static_cast<float>(kCurvePoints - 1);
        const float y = gainToY(curveDb[static_cast<size_t>(i)]);
        if (i == 0)
            main.startNewSubPath(x, y);
        else
            main.lineTo(x, y);
    }

    juce::Path fill(main);
    const float zeroY = gainToY(0.0);
    fill.lineTo(plot.getRight(), zeroY);
    fill.lineTo(plot.getX(), zeroY);
    fill.closeSubPath();

    g.setGradientFill(juce::ColourGradient(colourCurveFill().withAlpha(0.30f), 0.0f, plot.getY(),
                                           colourCurveFill().withAlpha(0.05f), 0.0f, plot.getBottom(), false));
    g.fillPath(fill);

    g.setColour(colourCurveLine());
    g.strokePath(main, juce::PathStrokeType(2.0f));
}

void EqView::drawNodes(juce::Graphics& g) {
    const int selected = proc.getSelectedBand();
    g.setFont(fontNode());

    for (int i = 0; i < kMaxBands; ++i) {
        const BandParams& bp = lastSnapshot.bands[i];
        const auto pos = nodePosition(i);
        const bool active = bp.enabled || bp.solo;
        const bool isSelected = (i == selected);
        const bool isHovered = (i == hoveredBand);

        // Idle bands stay visible as small dim markers so that empty slots can be
        // found and clicked without cluttering the graph.
        const float r = isSelected ? static_cast<float>(kSelectedNodeRadius)
                                   : (active || isHovered ? static_cast<float>(kNodeRadius) : 5.0f);

        juce::Colour fill = bp.solo ? colourNodeSolo()
                                    : (bp.enabled ? colourNodeEnabled() : colourNodeDisabled());

        if (isSelected || isHovered) {
            g.setColour(fill.withAlpha(0.18f));
            g.fillEllipse(juce::Rectangle<float>(pos.x - r - 5.0f, pos.y - r - 5.0f,
                                                 2.0f * (r + 5.0f), 2.0f * (r + 5.0f)));
        }

        g.setColour(fill.withAlpha(active ? 0.95f : 0.30f));
        g.fillEllipse(juce::Rectangle<float>(pos.x - r, pos.y - r, 2.0f * r, 2.0f * r));

        g.setColour(isSelected ? juce::Colours::white : fill.darker(0.5f).withAlpha(active ? 0.9f : 0.35f));
        g.drawEllipse(juce::Rectangle<float>(pos.x - r, pos.y - r, 2.0f * r, 2.0f * r), 1.6f);

        if (active || isSelected || isHovered) {
            g.setColour(isSelected ? juce::Colour(0xff101317) : juce::Colours::white.withAlpha(0.85f));
            g.drawText(juce::String(i + 1),
                       juce::Rectangle<float>(pos.x - r, pos.y - r, 2.0f * r, 2.0f * r),
                       juce::Justification::centred);
        }
    }
}

void EqView::drawOverlayText(juce::Graphics& g) {
    auto plot = plotArea();

    g.setFont(fontHint());

    juce::String hint = "double-click: add band   drag: freq/gain   ctrl+drag or wheel: Q   right-click: menu   S: solo";
    g.setColour(colourLabelText().withAlpha(0.55f));
    g.drawText(hint, plot.removeFromTop(16.0f).withTrimmedLeft(2.0f), juce::Justification::centredLeft);

    juce::String badge;
    juce::Colour badgeColour = juce::Colours::transparentWhite;
    if (lastSnapshot.globals.bypass) {
        badge = "BYPASS";
        badgeColour = juce::Colour(0xff9aa2ad);
    } else {
        for (int i = 0; i < kMaxBands; ++i) {
            if (lastSnapshot.bands[i].solo) {
                badge = "SOLO  " + juce::String(paramValue(ids::kSoloWidth), 2) + " oct";
                badgeColour = colourNodeSolo();
                break;
            }
        }
    }

    if (badge.isNotEmpty()) {
        g.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
        const auto box = juce::Rectangle<float>(plot.getRight() - 150.0f, plot.getY() + 2.0f,
                                                148.0f, 20.0f);
        g.setColour(badgeColour.withAlpha(0.20f));
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(badgeColour);
        g.drawText(badge, box, juce::Justification::centredRight);
    }

    if (lastSnapshot.globals.autoGain && !lastSnapshot.globals.bypass) {
        g.setFont(fontSmall());
        g.setColour(colourLabelText());
        g.drawText("auto gain", juce::Rectangle<float>(plot.getX() + 2.0f, plot.getBottom() - 14.0f,
                                                       80.0f, 12.0f),
                   juce::Justification::centredLeft);
    }
}

void EqView::paint(juce::Graphics& g) {
    drawBackground(g);

    if (!haveSnapshot)
        lastSnapshot = proc.snapshot();

    const auto plot = plotArea();
    const bool analyzerOn = paramValue(ids::kAnalyzerOn) > 0.5;
    const double rangeDb = analyzerRangeDb(
        juce::jlimit(0, 3, static_cast<int>(std::lround(paramValue(ids::kAnalyzerRange)))));

    // Grid lines and frequency labels live outside the clipped plot (labels sit in the
    // strip below it), so the grid is drawn first and unclipped.
    drawGrid(g);

    g.saveState();
    g.reduceClipRegion(juce::Rectangle<int>(juce::roundToInt(plot.getX()), juce::roundToInt(plot.getY()),
                                            juce::roundToInt(plot.getWidth()),
                                            juce::roundToInt(plot.getHeight())));
    if (analyzerOn) {
        if (haveInputSpectrum)
            drawSpectrum(g, inDisplay, colourInputSpectrum(), rangeDb);
        if (haveOutputSpectrum)
            drawSpectrum(g, outDisplay, colourOutputSpectrum(), rangeDb);
    }
    drawCurves(g);
    g.restoreState();

    drawNodes(g);
    drawOverlayText(g);
}

}  // namespace eqm1
