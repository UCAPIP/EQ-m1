#include "Processor/EqM1AudioProcessor.h"

#include <type_traits>

#include "Editor/EqM1AudioProcessorEditor.h"

namespace eqm1 {

EqM1AudioProcessor::EqM1AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "EQ_M1_STATE", createParameterLayout()) {
    cacheParameters();
    engine.setParameters(snapshot());
}

EqM1AudioProcessor::~EqM1AudioProcessor() = default;

void EqM1AudioProcessor::cacheParameters() {
    for (int i = 0; i < kMaxBands; ++i) {
        auto& c = bandParams[static_cast<size_t>(i)];
        c.type = apvts.getRawParameterValue(bandParamId(i, ids::kType));
        c.freq = apvts.getRawParameterValue(bandParamId(i, ids::kFreq));
        c.gain = apvts.getRawParameterValue(bandParamId(i, ids::kGain));
        c.q = apvts.getRawParameterValue(bandParamId(i, ids::kQ));
        c.slope = apvts.getRawParameterValue(bandParamId(i, ids::kSlope));
        c.channel = apvts.getRawParameterValue(bandParamId(i, ids::kChannel));
        c.on = apvts.getRawParameterValue(bandParamId(i, ids::kOn));
        c.solo = apvts.getRawParameterValue(bandParamId(i, ids::kSolo));
    }

    outGainParam = apvts.getRawParameterValue(ids::kOutGain);
    soloWidthParam = apvts.getRawParameterValue(ids::kSoloWidth);
    autoGainParam = apvts.getRawParameterValue(ids::kAutoGain);
    bypassParam = apvts.getRawParameterValue(ids::kBypass);
    analyzerOnParam = apvts.getRawParameterValue(ids::kAnalyzerOn);
    analyzerRangeParam = apvts.getRawParameterValue(ids::kAnalyzerRange);
    analyzerSpeedParam = apvts.getRawParameterValue(ids::kAnalyzerSpeed);
}

EngineInput EqM1AudioProcessor::snapshot() const {
    EngineInput in;
    in.numBands = kMaxBands;

    auto choice = [](std::atomic<float>* p, int maxIndex) {
        const int v = p != nullptr ? static_cast<int>(std::lround(p->load(std::memory_order_relaxed))) : 0;
        return juce::jlimit(0, maxIndex, v);
    };

    for (int i = 0; i < kMaxBands; ++i) {
        const auto& c = bandParams[static_cast<size_t>(i)];
        BandParams& b = in.bands[i];

        b.type = static_cast<FilterType>(choice(c.type, static_cast<int>(FilterType::count) - 1));
        b.channel = static_cast<ChannelMode>(choice(c.channel, static_cast<int>(ChannelMode::count) - 1));
        b.slope = static_cast<Slope>(choice(c.slope, static_cast<int>(Slope::count) - 1));
        b.frequency = c.freq != nullptr ? c.freq->load(std::memory_order_relaxed) : 1000.0;
        b.gainDb = c.gain != nullptr ? c.gain->load(std::memory_order_relaxed) : 0.0;
        b.q = c.q != nullptr ? c.q->load(std::memory_order_relaxed) : 0.707;
        b.enabled = c.on != nullptr && c.on->load(std::memory_order_relaxed) > 0.5f;
        b.solo = c.solo != nullptr && c.solo->load(std::memory_order_relaxed) > 0.5f;
    }

    in.globals.outputGainDb = outGainParam != nullptr ? outGainParam->load(std::memory_order_relaxed) : 0.0;
    in.globals.soloWidthOct = soloWidthParam != nullptr ? soloWidthParam->load(std::memory_order_relaxed) : 1.0;
    in.globals.autoGain = autoGainParam != nullptr && autoGainParam->load(std::memory_order_relaxed) > 0.5f;
    in.globals.bypass = bypassParam != nullptr && bypassParam->load(std::memory_order_relaxed) > 0.5f;
    return in;
}

void EqM1AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    analysisSampleRate = sampleRate;

    engine.prepare(sampleRate);
    engine.setParameters(snapshot());

    const int size = std::max(1, samplesPerBlock);
    inputAnalyser.prepare(sampleRate, size);
    outputAnalyser.prepare(sampleRate, size);

    monoScratchF.setSize(2, size, false, false, true);
    monoScratchD.setSize(2, size, false, false, true);
    monoScratchF.clear();
    monoScratchD.clear();
}

void EqM1AudioProcessor::releaseResources() {
    inputAnalyser.reset();
    outputAnalyser.reset();
}

bool EqM1AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;
    if (in.size() != out.size())
        return false;

    return in.size() == 1 || in.size() == 2;
}

template <typename SampleType>
void EqM1AudioProcessor::processMono(juce::AudioBuffer<SampleType>& scratch,
                                     juce::AudioBuffer<SampleType>& buffer, int numSamples) {
    if (scratch.getNumChannels() < 2 || scratch.getNumSamples() < numSamples)
        return;

    const SampleType* src = buffer.getReadPointer(0);
    SampleType* sl = scratch.getWritePointer(0);
    SampleType* sr = scratch.getWritePointer(1);

    for (int i = 0; i < numSamples; ++i) {
        sl[i] = src[i];
        sr[i] = src[i];
    }

    const SampleType* in[2] = {sl, sr};
    SampleType* out[2] = {sl, sr};

    inputAnalyser.pushBlock(in, 2, numSamples);
    engine.process(in, out, numSamples);
    outputAnalyser.pushBlock(out, 2, numSamples);

    SampleType* dst = buffer.getWritePointer(0);
    for (int i = 0; i < numSamples; ++i)
        dst[i] = sl[i];
}

template <typename SampleType>
void EqM1AudioProcessor::processTyped(juce::AudioBuffer<SampleType>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const bool analyzerOn = analyzerOnParam == nullptr
                         || analyzerOnParam->load(std::memory_order_relaxed) > 0.5f;
    inputAnalyser.setActive(analyzerOn);
    outputAnalyser.setActive(analyzerOn);

    if (numChannels >= 2) {
        const SampleType* in[2] = {buffer.getReadPointer(0), buffer.getReadPointer(1)};
        SampleType* out[2] = {buffer.getWritePointer(0), buffer.getWritePointer(1)};

        inputAnalyser.pushBlock(in, 2, numSamples);
        engine.process(in, out, numSamples);
        outputAnalyser.pushBlock(out, 2, numSamples);
        return;
    }

    if constexpr (std::is_same_v<SampleType, float>)
        processMono(monoScratchF, buffer, numSamples);
    else
        processMono(monoScratchD, buffer, numSamples);
}

void EqM1AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    engine.setParameters(snapshot());
    processTyped(buffer);
}

void EqM1AudioProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    engine.setParameters(snapshot());
    processTyped(buffer);
}

juce::AudioProcessorEditor* EqM1AudioProcessor::createEditor() {
    return new EqM1AudioProcessorEditor(*this);
}

bool EqM1AudioProcessor::hasEditor() const { return true; }

const juce::String EqM1AudioProcessor::getName() const { return kPluginName; }
bool EqM1AudioProcessor::acceptsMidi() const { return false; }
bool EqM1AudioProcessor::producesMidi() const { return false; }
bool EqM1AudioProcessor::isMidiEffect() const { return false; }
double EqM1AudioProcessor::getTailLengthSeconds() const { return 0.0; }

int EqM1AudioProcessor::getNumPrograms() {
    return juce::jmax(1, static_cast<int>(factoryPresets().size()));
}

int EqM1AudioProcessor::getCurrentProgram() { return currentProgram; }

const juce::String EqM1AudioProcessor::getProgramName(int index) {
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow(index, static_cast<int>(presets.size()))
               ? presets[static_cast<size_t>(index)].name
               : juce::String("Init");
}

void EqM1AudioProcessor::changeProgramName(int, const juce::String&) {}

void EqM1AudioProcessor::setCurrentProgram(int index) {
    const auto& presets = factoryPresets();
    if (!juce::isPositiveAndBelow(index, static_cast<int>(presets.size())))
        return;

    currentProgram = index;
    applyPreset(index);
}

void EqM1AudioProcessor::setParamValue(const juce::String& id, float realValue) {
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(realValue));
}

void EqM1AudioProcessor::applyPreset(int index) {
    const auto& presets = factoryPresets();
    if (!juce::isPositiveAndBelow(index, static_cast<int>(presets.size())))
        return;

    const Preset& preset = presets[static_cast<size_t>(index)];

    for (int i = 0; i < kMaxBands; ++i) {
        setParamValue(bandParamId(i, ids::kOn), 0.0f);
        setParamValue(bandParamId(i, ids::kSolo), 0.0f);
    }

    for (const auto& b : preset.bands) {
        if (!juce::isPositiveAndBelow(b.band, kMaxBands))
            continue;

        setParamValue(bandParamId(b.band, ids::kType), static_cast<float>(b.type));
        setParamValue(bandParamId(b.band, ids::kFreq), static_cast<float>(b.frequency));
        setParamValue(bandParamId(b.band, ids::kGain), static_cast<float>(b.gainDb));
        setParamValue(bandParamId(b.band, ids::kQ), static_cast<float>(b.q));
        setParamValue(bandParamId(b.band, ids::kSlope), static_cast<float>(b.slope));
        setParamValue(bandParamId(b.band, ids::kChannel), static_cast<float>(b.channel));
        setParamValue(bandParamId(b.band, ids::kOn), 1.0f);
    }

    setParamValue(ids::kOutGain, static_cast<float>(preset.outputGainDb));
    setParamValue(ids::kAutoGain, preset.autoGain ? 1.0f : 0.0f);
}

void EqM1AudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    const auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr)
        copyXmlToBinary(*xml, destData);
}

void EqM1AudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

}  // namespace eqm1
