#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>

#include "DSP/DspTypes.h"
#include "DSP/EqEngine.h"
#include "Processor/Params.h"
#include "Processor/SpectrumAnalyser.h"

namespace eqm1 {

class EqM1AudioProcessor : public juce::AudioProcessor {
public:
    EqM1AudioProcessor();
    ~EqM1AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- editor API ---------------------------------------------------------
    juce::AudioProcessorValueTreeState& getApvts() { return apvts; }
    EngineInput snapshot() const;
    double getAnalysisSampleRate() const noexcept { return analysisSampleRate; }

    bool readInputSpectrum(SpectrumAnalyser::Frame& out) { return inputAnalyser.readLatestFrame(out); }
    bool readOutputSpectrum(SpectrumAnalyser::Frame& out) { return outputAnalyser.readLatestFrame(out); }

    int getSelectedBand() const noexcept { return selectedBand.load(); }
    void setSelectedBand(int band) noexcept { selectedBand.store(juce::jlimit(-1, kMaxBands - 1, band)); }

private:
    struct BandParamCache {
        std::atomic<float> *type = nullptr, *freq = nullptr, *gain = nullptr, *q = nullptr;
        std::atomic<float> *slope = nullptr, *channel = nullptr, *on = nullptr, *solo = nullptr;
    };

    template <typename SampleType>
    void processTyped(juce::AudioBuffer<SampleType>& buffer);

    template <typename SampleType>
    void processMono(juce::AudioBuffer<SampleType>& scratch, juce::AudioBuffer<SampleType>& buffer,
                     int numSamples);

    void cacheParameters();
    void setParamValue(const juce::String& id, float realValue);
    void applyPreset(int index);

    juce::AudioProcessorValueTreeState apvts;
    EqEngine engine;
    MultiResAnalyser inputAnalyser, outputAnalyser;

    std::array<BandParamCache, kMaxBands> bandParams;
    std::atomic<float> *outGainParam = nullptr, *soloWidthParam = nullptr;
    std::atomic<float> *autoGainParam = nullptr, *bypassParam = nullptr;
    std::atomic<float> *analyzerOnParam = nullptr, *analyzerRangeParam = nullptr,
                       *analyzerSpeedParam = nullptr;

    juce::AudioBuffer<float> monoScratchF;
    juce::AudioBuffer<double> monoScratchD;
    double analysisSampleRate = 48000.0;
    int currentProgram = 0;
    std::atomic<int> selectedBand{0};
};

}  // namespace eqm1
