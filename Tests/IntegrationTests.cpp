// Headless integration tests for the JUCE layer: parameter layout, presets, state,
// the processor callback and editor painting. No window is ever shown.
#include <JuceHeader.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "DSP/DspTypes.h"
#include "Editor/EqM1AudioProcessorEditor.h"
#include "Processor/EqM1AudioProcessor.h"

namespace {

using namespace eqm1;

int gPassed = 0;
int gFailed = 0;

void section(const char* name) { std::printf("\n[%s]\n", name); }

void report(bool ok, const char* what, const std::string& detail = "") {
    if (ok) {
        ++gPassed;
        std::printf("  ok   %s\n", what);
    } else {
        ++gFailed;
        std::printf("  FAIL %s  (%s)\n", what, detail.c_str());
    }
}

void checkTrue(bool cond, const char* what, const std::string& detail = "") {
    report(cond, what, detail);
}

void checkNear(double actual, double expected, double tol, const char* what) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "got %.6f, want %.6f +/- %.6f", actual, expected, tol);
    report(std::fabs(actual - expected) <= tol, what, buf);
}

double bufferRms(const juce::AudioBuffer<float>& buffer, int channel) {
    double sum = 0.0;
    const int n = buffer.getNumSamples();
    const float* p = buffer.getReadPointer(channel);
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(p[i]) * p[i];
    return n > 0 ? std::sqrt(sum / n) : 0.0;
}

double bufferPeak(const juce::AudioBuffer<float>& buffer) {
    double peak = 0.0;
    for (int c = 0; c < buffer.getNumChannels(); ++c) {
        const float* p = buffer.getReadPointer(c);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = std::max(peak, std::fabs(static_cast<double>(p[i])));
    }
    return peak;
}

bool bufferIsFinite(const juce::AudioBuffer<float>& buffer) {
    for (int c = 0; c < buffer.getNumChannels(); ++c) {
        const float* p = buffer.getReadPointer(c);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (!std::isfinite(p[i]))
                return false;
    }
    return true;
}

void fillSine(juce::AudioBuffer<float>& buffer, double freq, double sampleRate, double amp) {
    for (int c = 0; c < buffer.getNumChannels(); ++c) {
        float* p = buffer.getWritePointer(c);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            p[i] = static_cast<float>(amp * std::sin(2.0 * kPi * freq * i / sampleRate));
    }
}

void fillNoise(juce::AudioBuffer<float>& buffer, uint32_t seed = 7u) {
    uint32_t s = seed;
    for (int c = 0; c < buffer.getNumChannels(); ++c) {
        float* p = buffer.getWritePointer(c);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            s = s * 1664525u + 1013904223u;
            p[i] = static_cast<float>(static_cast<double>(s) / 2147483648.0 - 1.0) * 0.3f;
        }
    }
}

void setRealValue(EqM1AudioProcessor& proc, const juce::String& id, double value) {
    setParamRealValue(proc.getApvts(), id, value);
}

// Pumps the message loop for a while so that component timers fire. JUCE disables modal
// loops on Linux, so runDispatchLoopUntil() is not available; a one-shot timer stops the
// loop instead.
void pumpMessages(int milliseconds) {
    juce::MessageManager::callAsync([milliseconds] {
        juce::Timer::callAfterDelay(milliseconds,
                                    [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
}

// ---------------------------------------------------------------------------
void testParameterLayout(EqM1AudioProcessor& proc) {
    section("parameter layout");

    const int expected = kMaxBands * 8 + 7;
    const int actual = proc.getParameters().size();
    checkTrue(actual == expected, "parameter count matches the design",
              "got " + std::to_string(actual) + " want " + std::to_string(expected));

    bool allNamed = true;
    for (auto* p : proc.getParameters())
        if (p->getName(64).isEmpty())
            allNamed = false;
    checkTrue(allNamed, "every parameter has a name");

    // The log-mapped ranges must round-trip: convertTo0to1(convertFrom0to1(x)) == x.
    double worstFreq = 0.0, worstQ = 0.0, worstWidth = 0.0, worstGain = 0.0;
    for (int i = 0; i <= 100; ++i) {
        const float n = static_cast<float>(i) / 100.0f;
        auto* freq = proc.getApvts().getParameter(bandParamId(0, ids::kFreq));
        auto* q = proc.getApvts().getParameter(bandParamId(0, ids::kQ));
        auto* width = proc.getApvts().getParameter(ids::kSoloWidth);
        auto* gain = proc.getApvts().getParameter(ids::kOutGain);

        worstFreq = std::max(worstFreq, std::fabs(static_cast<double>(freq->convertTo0to1(freq->convertFrom0to1(n))) - n));
        worstQ = std::max(worstQ, std::fabs(static_cast<double>(q->convertTo0to1(q->convertFrom0to1(n))) - n));
        worstWidth = std::max(worstWidth, std::fabs(static_cast<double>(width->convertTo0to1(width->convertFrom0to1(n))) - n));
        worstGain = std::max(worstGain, std::fabs(static_cast<double>(gain->convertTo0to1(gain->convertFrom0to1(n))) - n));
    }
    checkTrue(worstFreq < 1.0e-4, "frequency range round-trips", std::to_string(worstFreq));
    checkTrue(worstQ < 1.0e-4, "Q range round-trips", std::to_string(worstQ));
    checkTrue(worstWidth < 1.0e-4, "solo width range round-trips", std::to_string(worstWidth));
    checkTrue(worstGain < 1.0e-4, "output gain range round-trips", std::to_string(worstGain));

    // Range extremes must land exactly on the documented bounds.
    auto* freq = proc.getApvts().getParameter(bandParamId(0, ids::kFreq));
    checkNear(freq->convertFrom0to1(0.0f), 10.0, 0.01, "frequency 0.0 -> 10 Hz");
    checkNear(freq->convertFrom0to1(1.0f), 30000.0, 1.0, "frequency 1.0 -> 30 kHz");
    checkNear(freq->convertFrom0to1(0.5f), std::sqrt(10.0 * 30000.0), 2.0,
              "frequency 0.5 -> geometric centre");

    auto* q = proc.getApvts().getParameter(bandParamId(0, ids::kQ));
    checkNear(q->convertFrom0to1(0.0f), 0.05, 1.0e-4, "Q 0.0 -> 0.05");
    checkNear(q->convertFrom0to1(1.0f), 24.0, 1.0e-2, "Q 1.0 -> 24");

    // A fresh instance must be transparent: no band enabled.
    const EngineInput snap = proc.snapshot();
    int enabled = 0;
    for (int i = 0; i < kMaxBands; ++i)
        if (snap.bands[i].enabled)
            ++enabled;
    checkTrue(enabled == 0, "a fresh instance has no enabled bands", std::to_string(enabled));
    checkNear(snap.globals.outputGainDb, 0.0, 1.0e-6, "output gain defaults to 0 dB");
    checkTrue(!snap.globals.bypass, "bypass defaults to off");
    checkTrue(!snap.globals.autoGain, "auto gain defaults to off");
}

void testProcessing(EqM1AudioProcessor& proc) {
    section("processing");
    const double fs = 48000.0;
    proc.prepareToPlay(fs, 512);

    // 480 samples hold exactly ten periods of a 1 kHz tone at 48 kHz, so the RMS of the
    // block equals amplitude/sqrt(2) and the level checks below are exact.
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 480);

    // Transparent by default.
    fillSine(buffer, 1000.0, fs, 0.5);
    proc.processBlock(buffer, midi);
    checkTrue(bufferIsFinite(buffer), "default state produces finite output");
    checkNear(bufferRms(buffer, 0), 0.5 / std::sqrt(2.0), 0.01, "default state is transparent");

    // Enable a +6 dB bell at 1 kHz.
    setRealValue(proc, bandParamId(0, ids::kType), static_cast<double>(FilterType::Bell));
    setRealValue(proc, bandParamId(0, ids::kFreq), 1000.0);
    setRealValue(proc, bandParamId(0, ids::kGain), 6.0);
    setRealValue(proc, bandParamId(0, ids::kQ), 1.0);
    setRealValue(proc, bandParamId(0, ids::kOn), 1.0);

    // Run enough blocks for the parameter smoothers to settle.
    for (int i = 0; i < 40; ++i) {
        fillSine(buffer, 1000.0, fs, 0.5);
        proc.processBlock(buffer, midi);
    }
    checkTrue(bufferIsFinite(buffer), "boosted output is finite");
    checkNear(bufferRms(buffer, 0) / (0.5 / std::sqrt(2.0)), 2.0, 0.05,
              "+6 dB bell doubles the level of a 1 kHz sine");

    // Solo monitoring through the real plugin wiring.
    setRealValue(proc, bandParamId(0, ids::kSolo), 1.0);
    setRealValue(proc, ids::kSoloWidth, 0.5);
    for (int i = 0; i < 40; ++i) {
        fillSine(buffer, 1000.0, fs, 0.5);
        proc.processBlock(buffer, midi);
    }
    const double inBandRms = bufferRms(buffer, 0);

    for (int i = 0; i < 40; ++i) {
        fillSine(buffer, 8000.0, fs, 0.5);
        proc.processBlock(buffer, midi);
    }
    const double outBandRms = bufferRms(buffer, 0);

    checkTrue(inBandRms > 0.1, "solo passes the in-band tone", std::to_string(inBandRms));
    checkTrue(outBandRms < inBandRms * 0.02, "solo rejects an out-of-band tone",
              "in=" + std::to_string(inBandRms) + " out=" + std::to_string(outBandRms));

    // Undo the solo and the band settings for the following tests.
    setRealValue(proc, bandParamId(0, ids::kSolo), 0.0);
    setRealValue(proc, bandParamId(0, ids::kOn), 0.0);

    // Bypass must be transparent even with a big boost armed.
    setRealValue(proc, bandParamId(1, ids::kType), static_cast<double>(FilterType::Bell));
    setRealValue(proc, bandParamId(1, ids::kFreq), 2000.0);
    setRealValue(proc, bandParamId(1, ids::kGain), 24.0);
    setRealValue(proc, bandParamId(1, ids::kOn), 1.0);
    setRealValue(proc, ids::kBypass, 1.0);
    for (int i = 0; i < 40; ++i) {
        fillSine(buffer, 2000.0, fs, 0.25);
        proc.processBlock(buffer, midi);
    }
    checkNear(bufferRms(buffer, 0), 0.25 / std::sqrt(2.0), 0.005, "bypass is transparent");
    setRealValue(proc, ids::kBypass, 0.0);
    setRealValue(proc, bandParamId(1, ids::kOn), 0.0);
}

void testBusLayoutsAndMono(EqM1AudioProcessor& proc) {
    section("bus layouts");

    auto makeLayout = [](juce::AudioChannelSet in, juce::AudioChannelSet out) {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add(in);
        layout.outputBuses.add(out);
        return layout;
    };

    checkTrue(proc.isBusesLayoutSupported(makeLayout(juce::AudioChannelSet::stereo(),
                                                     juce::AudioChannelSet::stereo())),
              "stereo in/out supported");
    checkTrue(proc.isBusesLayoutSupported(makeLayout(juce::AudioChannelSet::mono(),
                                                     juce::AudioChannelSet::mono())),
              "mono in/out supported");
    checkTrue(!proc.isBusesLayoutSupported(makeLayout(juce::AudioChannelSet::quadraphonic(),
                                                      juce::AudioChannelSet::quadraphonic())),
              "quad layout rejected");
    checkTrue(!proc.isBusesLayoutSupported(makeLayout(juce::AudioChannelSet::disabled(),
                                                      juce::AudioChannelSet::stereo())),
              "disabled input rejected");
    checkTrue(!proc.isBusesLayoutSupported(makeLayout(juce::AudioChannelSet::mono(),
                                                      juce::AudioChannelSet::stereo())),
              "mismatched in/out rejected");

    // Mono path: run through the internal upmix.
    auto mono = makeLayout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono());
    if (proc.setBusesLayout(mono)) {
        proc.prepareToPlay(48000.0, 256);
        juce::MidiBuffer midi;
        // 240 samples = exactly five periods of 1 kHz at 48 kHz.
        juce::AudioBuffer<float> buffer(1, 240);
        setRealValue(proc, bandParamId(2, ids::kType), static_cast<double>(FilterType::Bell));
        setRealValue(proc, bandParamId(2, ids::kFreq), 1000.0);
        setRealValue(proc, bandParamId(2, ids::kGain), 6.0);
        setRealValue(proc, bandParamId(2, ids::kChannel), static_cast<double>(ChannelMode::MidOnly));
        setRealValue(proc, bandParamId(2, ids::kOn), 1.0);

        for (int i = 0; i < 60; ++i) {
            fillSine(buffer, 1000.0, 48000.0, 0.4);
            proc.processBlock(buffer, midi);
        }
        checkTrue(bufferIsFinite(buffer), "mono processing is finite");
        checkNear(bufferRms(buffer, 0) / (0.4 / std::sqrt(2.0)), 2.0, 0.05,
                  "mono signal is treated as mid and gains +6 dB");

        setRealValue(proc, bandParamId(2, ids::kOn), 0.0);

        auto stereo = makeLayout(juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
        proc.setBusesLayout(stereo);
        proc.prepareToPlay(48000.0, 512);
    } else {
        checkTrue(false, "mono bus layout could not be set");
    }
}

void testPresets(EqM1AudioProcessor& proc) {
    section("presets");
    const int count = proc.getNumPrograms();
    checkTrue(count == static_cast<int>(factoryPresets().size()), "program count matches the preset list",
              "got " + std::to_string(count));
    checkTrue(count > 1, "more than one preset is available");

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 512);
    proc.prepareToPlay(48000.0, 512);

    for (int i = 0; i < count; ++i) {
        proc.setCurrentProgram(i);
        const juce::String name = proc.getProgramName(i);

        bool finite = true;
        double peak = 0.0;
        for (int b = 0; b < 30; ++b) {
            fillNoise(buffer, static_cast<uint32_t>(1000 + i));
            proc.processBlock(buffer, midi);
            if (!bufferIsFinite(buffer))
                finite = false;
            peak = std::max(peak, bufferPeak(buffer));
        }

        checkTrue(finite, ("preset '" + name.toStdString() + "' produces finite audio").c_str());
        checkTrue(peak < 20.0, ("preset '" + name.toStdString() + "' stays in a sane range").c_str(),
                  "peak=" + std::to_string(peak));

        // A preset other than Init must actually enable bands.
        const EngineInput snap = proc.snapshot();
        int enabled = 0;
        for (int b = 0; b < kMaxBands; ++b)
            if (snap.bands[b].enabled)
                ++enabled;
        if (i > 0)
            checkTrue(enabled > 0, ("preset '" + name.toStdString() + "' enables bands").c_str(),
                      "enabled=" + std::to_string(enabled));
        else
            checkTrue(enabled == 0, "Init preset leaves every band off");
    }

    proc.setCurrentProgram(0);
}

void testStateRoundTrip(EqM1AudioProcessor& proc) {
    section("state save / restore");

    setRealValue(proc, bandParamId(3, ids::kType), static_cast<double>(FilterType::HighShelf));
    setRealValue(proc, bandParamId(3, ids::kFreq), 8200.0);
    setRealValue(proc, bandParamId(3, ids::kGain), -7.5);
    setRealValue(proc, bandParamId(3, ids::kQ), 1.3);
    setRealValue(proc, bandParamId(3, ids::kChannel), static_cast<double>(ChannelMode::SideOnly));
    setRealValue(proc, bandParamId(3, ids::kSlope), static_cast<double>(Slope::dB48));
    setRealValue(proc, bandParamId(3, ids::kOn), 1.0);
    setRealValue(proc, ids::kOutGain, -3.0);
    setRealValue(proc, ids::kAutoGain, 1.0);
    setRealValue(proc, ids::kSoloWidth, 2.5);

    juce::MemoryBlock block;
    proc.getStateInformation(block);
    checkTrue(block.getSize() > 0, "state is not empty", std::to_string(block.getSize()));

    EqM1AudioProcessor restored;
    restored.setStateInformation(block.getData(), static_cast<int>(block.getSize()));

    const EngineInput a = proc.snapshot();
    const EngineInput b = restored.snapshot();
    checkTrue(a == b, "restored parameters match the original snapshot exactly");

    // A corrupt blob must not crash or change anything.
    EqM1AudioProcessor victim;
    const EngineInput before = victim.snapshot();
    char garbage[64] = {1, 2, 3, 4, 5};
    victim.setStateInformation(garbage, sizeof(garbage));
    checkTrue(before == victim.snapshot(), "garbage state is ignored");

    // Reset the shared instance back to a clean slate.
    setRealValue(proc, bandParamId(3, ids::kOn), 0.0);
    setRealValue(proc, ids::kOutGain, 0.0);
    setRealValue(proc, ids::kAutoGain, 0.0);
    setRealValue(proc, ids::kSoloWidth, 1.0);
}

void testAutomationExtremes(EqM1AudioProcessor& proc) {
    section("automation extremes");

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 256);
    proc.prepareToPlay(48000.0, 256);

    bool finite = true;
    double peak = 0.0;

    for (auto* p : proc.getParameters()) {
        for (float v : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            p->setValue(v);
            for (int b = 0; b < 4; ++b) {
                fillNoise(buffer);
                proc.processBlock(buffer, midi);
                if (!bufferIsFinite(buffer))
                    finite = false;
                peak = std::max(peak, bufferPeak(buffer));
            }
        }
        p->setValue(p->getDefaultValue());
    }

    checkTrue(finite, "every parameter at every extreme stays finite");
    checkTrue(peak < 60.0, "no runaway gain at parameter extremes", "peak=" + std::to_string(peak));

    // Sample-rate change must re-prepare cleanly.
    proc.prepareToPlay(192000.0, 128);
    fillSine(buffer, 30000.0, 192000.0, 0.4);
    proc.processBlock(buffer, midi);
    checkTrue(bufferIsFinite(buffer), "192 kHz processing is finite");

    proc.prepareToPlay(44100.0, 512);
    fillSine(buffer, 21000.0, 44100.0, 0.4);
    proc.processBlock(buffer, midi);
    checkTrue(bufferIsFinite(buffer), "a band above Nyquist at 44.1 kHz stays finite");
}

void testEditorPainting(EqM1AudioProcessor& proc) {
    section("editor");

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    checkTrue(editor != nullptr, "createEditor returns an editor");
    if (editor == nullptr)
        return;

    auto paintedPixels = [](const juce::Image& img) {
        const juce::Image::BitmapData data(img, juce::Image::BitmapData::readOnly);
        int nonEmpty = 0;
        for (int y = 0; y < img.getHeight(); y += 7) {
            for (int x = 0; x < img.getWidth(); x += 7) {
                if (data.getPixelColour(x, y).getAlpha() > 0)
                    ++nonEmpty;
            }
        }
        return nonEmpty;
    };

    auto renderAt = [&](int width, int height) {
        editor->setSize(width, height);
        pumpMessages(120);

        juce::Image img(juce::Image::ARGB, width, height, true);
        {
            // The Graphics context must be destroyed before the pixels are read: on
            // Windows JUCE renders through Direct2D and only flushes the surface then.
            juce::Graphics g(img);
            editor->paintEntireComponent(g, true);
        }
        return paintedPixels(img);
    };

    const int pixelsAt1024 = renderAt(1024, 620);
    checkTrue(pixelsAt1024 > 100, "editor paints content at 1024x620",
              "painted=" + std::to_string(pixelsAt1024));
    checkTrue(renderAt(820, 470) > 100, "editor paints content at the minimum size");
    checkTrue(renderAt(1800, 1100) > 100, "editor paints content when maximised");

    // Painting must survive every band being selected, enabled and soloed.
    for (int band = 0; band < kMaxBands; ++band) {
        proc.setSelectedBand(band);
        setRealValue(proc, bandParamId(band, ids::kOn), 1.0);
        setRealValue(proc, bandParamId(band, ids::kGain), 12.0 - band);
    }
    checkTrue(renderAt(1200, 700) > 100, "editor paints with 16 enabled bands");

    setRealValue(proc, bandParamId(4, ids::kType), static_cast<double>(FilterType::LowPass));
    setRealValue(proc, bandParamId(4, ids::kSlope), static_cast<double>(Slope::dB96));
    setRealValue(proc, bandParamId(9, ids::kType), static_cast<double>(FilterType::Notch));
    setRealValue(proc, bandParamId(9, ids::kSolo), 1.0);
    checkTrue(renderAt(1200, 700) > 100, "editor paints with a solo badge and mixed filter types");

    setRealValue(proc, ids::kBypass, 1.0);
    checkTrue(renderAt(1200, 700) > 100, "editor paints in bypass");
    setRealValue(proc, ids::kBypass, 0.0);

    setRealValue(proc, bandParamId(9, ids::kSolo), 0.0);
    setRealValue(proc, ids::kAnalyzerOn, 0.0);
    checkTrue(renderAt(900, 560) > 100, "editor paints with the analyzer off");
    setRealValue(proc, ids::kAnalyzerOn, 1.0);

    // Deselecting must not break the control bar.
    proc.setSelectedBand(-1);
    checkTrue(renderAt(1024, 620) > 100, "editor paints with no band selected");

    proc.setSelectedBand(0);
    editor.reset();
    checkTrue(true, "editor destruction is clean");
}

void testSpectrumPipeline(EqM1AudioProcessor& proc) {
    section("spectrum pipeline");

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2, 512);
    proc.prepareToPlay(48000.0, 512);

    setRealValue(proc, bandParamId(0, ids::kOn), 0.0);
    setRealValue(proc, ids::kAnalyzerOn, 1.0);

    // Feed a loud 1 kHz tone and let the analyser produce frames.
    for (int b = 0; b < 40; ++b) {
        fillSine(buffer, 1000.0, 48000.0, 0.9);
        proc.processBlock(buffer, midi);
    }
    pumpMessages(150);

    SpectrumAnalyser::Frame frame;
    bool gotInput = false, gotOutput = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        gotInput |= proc.readInputSpectrum(frame);
        gotOutput |= proc.readOutputSpectrum(frame);
        if (gotInput && gotOutput)
            break;
        for (int b = 0; b < 4; ++b) {
            fillSine(buffer, 1000.0, 48000.0, 0.9);
            proc.processBlock(buffer, midi);
        }
        pumpMessages(40);
    }

    checkTrue(gotInput, "input analyser produces frames");
    checkTrue(gotOutput, "output analyser produces frames");

    int peakBin = 0;
    for (int i = 1; i < kSpectrumBins; ++i)
        if (frame[static_cast<size_t>(i)] > frame[static_cast<size_t>(peakBin)])
            peakBin = i;

    const double peakFreq =
        frequencyForDisplayBin(peakBin, kSpectrumBins, kSpectrumMinFreq, kSpectrumMaxFreq);
    checkTrue(peakFreq > 800.0 && peakFreq < 1250.0,
              "analyser peak lands on the 1 kHz tone", std::to_string(peakFreq));
    checkTrue(frame[static_cast<size_t>(peakBin)] > -12.0,
              "analyser reports a healthy level for a 0.9 amplitude tone",
              std::to_string(frame[static_cast<size_t>(peakBin)]));
}

}  // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    std::printf("EQ-m1 integration tests (JUCE layer)\n");

    EqM1AudioProcessor proc;
    testParameterLayout(proc);
    testProcessing(proc);
    testBusLayoutsAndMono(proc);
    testPresets(proc);
    testStateRoundTrip(proc);
    testAutomationExtremes(proc);
    testSpectrumPipeline(proc);
    testEditorPainting(proc);

    std::printf("\n-----------------------------------------\n");
    std::printf("passed: %d   failed: %d\n", gPassed, gFailed);
    std::printf(gFailed == 0 ? "RESULT: OK\n" : "RESULT: FAILED\n");
    return gFailed == 0 ? 0 : 1;
}
