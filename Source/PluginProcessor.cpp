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
    startTimerHz (10);
}

PowerCompProcessor::~PowerCompProcessor()
{
    stopTimer();
    if (worker.joinable())
        worker.join();
}

bool PowerCompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void PowerCompProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate.store (sampleRate);
    // LEARN capture: up to 20 s of sound at this rate (allocated here, never on the audio thread).
    if (worker.joinable())
        worker.join();
    if (getLearnState() == LearnState::listening)
        learnState.store ((int) LearnState::idle);
    if (! juce::exactlyEqual (captureRate, sampleRate) || capCapacity == 0)
    {
        capCapacity = (int) (pc::learn::maxSeconds * sampleRate);
        capL.assign ((size_t) capCapacity, 0.0f);
        capR.assign ((size_t) capCapacity, 0.0f);
        capPos.store (0);
        captureRate = sampleRate;
    }
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

    // LEARN: copy the sounding input (silence skipped) until the capture is full.
    if (learnState.load() == (int) LearnState::listening && capCapacity > 0)
    {
        float pk = 0.0f;
        for (int c = 0; c < chans; ++c) pk = std::max (pk, buffer.getMagnitude (c, 0, n));
        if (pk > 0.001f)
        {
            const int pos = capPos.load(), m = std::min (n, capCapacity - pos);
            std::copy (ch[0], ch[0] + m, capL.begin() + pos);
            std::copy (ch[chans > 1 ? 1 : 0], ch[chans > 1 ? 1 : 0] + m, capR.begin() + pos);
            capPos.store (pos + m);
            if (pos + m >= capCapacity)
                learnState.store ((int) LearnState::analysing);
        }
    }

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
    haveLearn = learnedOn = false; // a preset ends the BEFORE | LEARNED comparison
}

//==============================================================================
void PowerCompProcessor::startLearning()
{
    if (getLearnState() != LearnState::idle || worker.joinable() || capCapacity == 0)
        return;
    beforeSnap = pc::captureSnapshot (state);
    beforeName = getPresetName();
    haveLearn = learnedOn = false;
    capPos.store (0);
    learnState.store ((int) LearnState::listening);
}

void PowerCompProcessor::stopLearning()
{
    if (getLearnState() != LearnState::listening)
        return;
    learnState.store ((int) (learnedSeconds() >= pc::learn::minSeconds ? LearnState::analysing : LearnState::idle));
}

pc::learn::Profile PowerCompProcessor::getLearnProfile() const
{
    return (pc::learn::Profile) juce::jlimit (0, 5, (int) state.state.getProperty ("learnProfile", 0));
}

float PowerCompProcessor::getLearnAmount() const
{
    return juce::jlimit (0.0f, 1.0f, (float) (double) state.state.getProperty ("learnAmount", 0.5));
}

void PowerCompProcessor::setLearnProfile (pc::learn::Profile p)
{
    state.state.setProperty ("learnProfile", (int) p, nullptr);
    rerunWanted = haveLearn;
}

void PowerCompProcessor::setLearnAmount (float a)
{
    state.state.setProperty ("learnAmount", (double) juce::jlimit (0.0f, 1.0f, a), nullptr);
    rerunWanted = haveLearn;
}

void PowerCompProcessor::pollLearn()
{
    if (workerDone.load())
    {
        worker.join();
        workerDone = false;
        learnResult = workerResult;
        if (learnResult.valid)
            applySuggestion (learnResult);
        learnState.store ((int) LearnState::idle);
    }
    // A new AMOUNT or profile after a LEARN: the same capture again (once the previous analysis is done).
    if (rerunWanted.load() && ! worker.joinable() && getLearnState() == LearnState::idle && haveLearn)
    {
        rerunWanted = false;
        learnState.store ((int) LearnState::analysing);
    }
    if (getLearnState() == LearnState::analysing && ! worker.joinable())
    {
        const int n = capPos.load();
        const auto current = pc::readParams (state);
        const auto profile = getLearnProfile();
        const float amount = getLearnAmount();
        const double sr = captureRate;
        worker = std::thread ([this, n, current, profile, amount, sr]
        {
            workerResult = pc::learn::analyse (capL.data(), capR.data(), n, sr, profile, amount, current);
            workerDone = true;
        });
    }
}

void PowerCompProcessor::waitForLearn()
{
    for (int i = 0; i < 3; ++i)
    {
        pollLearn();
        if (worker.joinable())
            while (! workerDone.load()) std::this_thread::sleep_for (std::chrono::milliseconds (5));
        pollLearn();
    }
}

void PowerCompProcessor::applySuggestion (const pc::learn::Suggestion& s)
{
    pc::Snapshot v;
    const auto& p = s.params;
    for (int b = 0; b < pc::numBands; ++b)
    {
        v[pc::id::comp[b]] = p.comp[(size_t) b];
        v[pc::id::gain[b]] = p.gainDb[(size_t) b];
        v[pc::id::solo[b]] = 0.0f;
        v[pc::id::bypass[b]] = 0.0f;
    }
    v[pc::id::lowSplit] = p.lowSplitHz;
    v[pc::id::highSplit] = p.highSplitHz;
    v[pc::id::speed] = p.speed;
    v[pc::id::upward] = p.upward;
    v[pc::id::mix] = p.mix;
    v[pc::id::output] = p.outputDb;
    v[pc::id::link] = p.link;
    pc::applySnapshot (state, v);
    learnedSnap = pc::captureSnapshot (state);
    learnedName = "LEARN: " + pc::learn::profileNames()[(int) s.profile];
    setPresetName (learnedName);
    haveLearn = learnedOn = true;
}

void PowerCompProcessor::showLearned (bool learned)
{
    if (! haveLearn || learned == learnedOn)
        return;
    // Each side keeps the changes made on it.
    (learnedOn ? learnedSnap : beforeSnap) = pc::captureSnapshot (state);
    pc::applySnapshot (state, learned ? learnedSnap : beforeSnap);
    setPresetName (learned ? learnedName : beforeName);
    learnedOn = learned;
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
