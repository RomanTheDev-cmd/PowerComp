#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/CompEngine.h"
#include "DSP/Learn.h"
#include "Presets.h"

class PowerCompProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PowerCompProcessor();
    ~PowerCompProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PowerComp"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return getPresetName(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void loadPreset (int index);

    // LEARN (2026-10-28): listens to the track (a copy of the input; the sound does not change), analyses off the audio
    // thread and puts the result on the ordinary parameters. BEFORE | LEARNED compare with the setting before LEARN.
    // Message thread, except what the audio thread does in processBlock.
    enum class LearnState { idle, listening, analysing };
    void startLearning();
    void stopLearning();                  // analyse what was heard (at least 2 s), otherwise cancel
    LearnState getLearnState() const { return (LearnState) learnState.load(); }
    double learnedSeconds() const { return capPos.load() / std::max (1.0, captureRate); }
    bool hasLearned() const { return haveLearn; }
    bool isShowingLearned() const { return learnedOn; }
    void showLearned (bool learned);      // BEFORE | LEARNED
    const pc::learn::Suggestion& lastLearn() const { return learnResult; }
    pc::learn::Profile getLearnProfile() const;
    float getLearnAmount() const;
    void setLearnProfile (pc::learn::Profile);   // re-applies from the same capture after a LEARN
    void setLearnAmount (float);
    void pollLearn();                     // starts the analysis and applies its result (the timer calls it)
    void waitForLearn();                  // blocks until nothing is pending (tests, headless)

    juce::AudioProcessorValueTreeState state;
    pc::PresetLibrary library;
    // The preset name is read by the host from its own thread: guarded.
    juce::String getPresetName() const { const juce::SpinLock::ScopedLockType l (nameLock); return presetName; }
    void setPresetName (const juce::String& n) { const juce::SpinLock::ScopedLockType l (nameLock); presetName = n; }

    // For the editor (the audio thread writes, the editor reads and clears).
    // Largest reduction / boost of each band since the editor last looked, in dB.
    std::array<std::atomic<float>, pc::numBands> reductionDb {}, boostDb {};
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };
    // The last input and output samples (mono), for the spectrum: lock-free rings.
    static constexpr int ringSize = 4096;
    std::array<float, ringSize> inRing {}, outRing {};
    std::atomic<int> ringWrite { 0 };
    std::atomic<double> currentRate { 48000.0 };

private:
    void timerCallback() override { pollLearn(); }
    void applySuggestion (const pc::learn::Suggestion&);
    std::vector<float> capL, capR;
    std::atomic<int> capPos { 0 }, learnState { 0 };
    int capCapacity = 0;
    double captureRate = 48000.0;
    std::thread worker;
    std::atomic<bool> workerDone { false }, rerunWanted { false };
    pc::learn::Suggestion workerResult, learnResult;
    pc::Snapshot beforeSnap, learnedSnap;
    juce::String beforeName, learnedName;
    bool haveLearn = false, learnedOn = false;
    juce::String presetName { "Gentle Glue" };
    mutable juce::SpinLock nameLock;
    pc::dsp::CompEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerCompProcessor)
};
