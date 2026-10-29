#include <JuceHeader.h>

#include "Processor/EqM1AudioProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new eqm1::EqM1AudioProcessor();
}
