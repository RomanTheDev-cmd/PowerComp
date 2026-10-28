#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/CompEngine.h"
#include "Presets.h"

class PowerCompProcessor : public juce::AudioProcessor
{
public:
    PowerCompProcessor();
    ~PowerCompProcessor() override = default;

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
    juce::String presetName { "Gentle Glue" };
    mutable juce::SpinLock nameLock;
    pc::dsp::CompEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerCompProcessor)
};
