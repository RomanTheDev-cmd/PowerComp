#include "PluginProcessor.h"
#include "PluginEditor.h"

PowerCompProcessor::PowerCompProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "PowerComp", pc::createLayout())
{
    if (const int i = library.indexOf (getPresetName()); i >= 0)
        pc::applySnapshot (state, library.all()[(size_t) i].values);
}

bool PowerCompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void PowerCompProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate.store (sampleRate);
    engine.prepare (sampleRate, samplesPerBlock, pc::readParams (state));
    setLatencySamples (pc::dsp::CompEngine::getLatencySamples());
}

void PowerCompProcessor::reset()
{
    engine.reset();
    for (int b = 0; b < pc::numBands; ++b)
    {
        reductionDb[(size_t) b].store (0.0f);
        boostDb[(size_t) b].store (0.0f);
    }
    meterL.store (0.0f);
    meterR.store (0.0f);
}

void PowerCompProcessor::releaseResources()
{
    reset();
}

void PowerCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int ins = getTotalNumInputChannels(), outs = std::min (buffer.getNumChannels(), getTotalNumOutputChannels());
    for (int c = ins; c < outs; ++c)
        buffer.clear (c, 0, n);
    if (n == 0 || outs == 0)
        return;
    const int chans = std::min (2, outs);
    float* ch[2] = { buffer.getWritePointer (0), chans > 1 ? buffer.getWritePointer (1) : nullptr };

    int w = ringWrite.load (std::memory_order_relaxed);
    for (int i = 0, k = w; i < n; ++i, k = (k + 1) & (ringSize - 1))
        inRing[(size_t) k] = chans > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];

    engine.process (ch, chans, n, pc::readParams (state));

    for (int i = 0; i < n; ++i)
    {
        outRing[(size_t) w] = chans > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
        w = (w + 1) & (ringSize - 1);
    }
    ringWrite.store (w, std::memory_order_release);
    for (int c = 2; c < outs; ++c)
        buffer.clear (c, 0, n);

    for (size_t b = 0; b < (size_t) pc::numBands; ++b)
    {
        reductionDb[b].store (std::max (reductionDb[b].load(), engine.blockReductionDb[b]));
        boostDb[b].store (std::max (boostDb[b].load(), engine.blockBoostDb[b]));
    }
    auto peak = [n] (const float* x)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (x, n);
        return std::max (std::abs (range.getStart()), std::abs (range.getEnd()));
    };
    meterL.store (std::max (peak (ch[0]), meterL.load() * 0.85f));
    meterR.store (std::max (chans > 1 ? peak (ch[1]) : peak (ch[0]), meterR.load() * 0.85f));
}

void PowerCompProcessor::loadPreset (int index)
{
    const auto& all = library.all();
    if (index < 0 || index >= (int) all.size())
        return;
    pc::applySnapshot (state, all[(size_t) index].values);
    setPresetName (all[(size_t) index].name);
}

juce::AudioProcessorEditor* PowerCompProcessor::createEditor() { return new PowerCompEditor (*this); }

void PowerCompProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.setProperty ("presetName", getPresetName(), nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void PowerCompProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            setPresetName (tree.getProperty ("presetName", getPresetName()).toString());
            // Parameters added after this project was saved come back neutral (old sound).
            juce::StringArray missing;
            for (const auto& [pid, value] : pc::legacyNeutral())
                if (xml->getChildByAttribute ("id", pid) == nullptr)
                    missing.add (pid);
            state.replaceState (tree);
            for (const auto& pid : missing)
                if (auto* param = state.getParameter (pid))
                    param->setValueNotifyingHost (param->convertTo0to1 (pc::legacyNeutral().at (pid)));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PowerCompProcessor(); }
