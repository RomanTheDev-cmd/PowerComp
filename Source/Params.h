#pragma once

#include <array>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

namespace pc
{
inline constexpr int numBands = 3; // 0 = LOW, 1 = MID, 2 = HIGH

namespace id
{
    // Per band: [band] = low, mid, high
    inline constexpr const char* comp[numBands]   = { "lowComp", "midComp", "highComp" };
    inline constexpr const char* gain[numBands]   = { "lowGain", "midGain", "highGain" };
    inline constexpr const char* solo[numBands]   = { "lowSolo", "midSolo", "highSolo" };
    inline constexpr const char* bypass[numBands] = { "lowBypass", "midBypass", "highBypass" };
    // CROSSOVER
    inline constexpr const char* lowSplit  = "lowSplit";
    inline constexpr const char* highSplit = "highSplit";
    // DYNAMICS / OUTPUT
    inline constexpr const char* speed    = "speed";
    inline constexpr const char* upward   = "upward";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* output   = "output";
    // ADVANCED and the header
    inline constexpr const char* autoGain = "autoGain";
    inline constexpr const char* link     = "link";
    inline constexpr const char* analog   = "analog";
}

// Crossover ranges do not overlap, so LOW|MID is always below MID|HIGH.
inline constexpr float lowSplitMin = 30.0f, lowSplitMax = 600.0f;
inline constexpr float highSplitMin = 800.0f, highSplitMax = 12000.0f;

// The sound of one instance, read once per block. The defaults here ARE the parameter defaults (createLayout reads
// them), so a preset that leaves a parameter out and a fresh instance agree.
struct CompParams
{
    std::array<float, numBands> comp { 0.3f, 0.3f, 0.3f };   // COMPRESS 0..1
    std::array<float, numBands> gainDb { 0.0f, 0.0f, 0.0f }; // band level after compression, -12..+12 dB
    std::array<bool, numBands> solo { false, false, false };
    std::array<bool, numBands> bypass { false, false, false };
    float lowSplitHz = 150.0f;
    float highSplitHz = 2500.0f;
    float speed = 0.5f;     // 0 slow (more punch) .. 1 fast (denser)
    float upward = 0.0f;    // 0..1, lifts quiet parts
    float mix = 1.0f;       // 0 dry .. 1 compressed
    float outputDb = 0.0f;  // -24..+12 dB
    bool autoGain = true;   // compensation of the compression, per band
    float link = 1.0f;      // stereo link 0..1
    bool analog = false;    // DIGITAL | ANALOGUE
};

const juce::StringArray& allParamIds();
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
CompParams readParams (const juce::AudioProcessorValueTreeState& state);
// The one mapping from plain values to CompParams; get (id, default) returns the plain value of a parameter.
CompParams paramsFrom (const std::function<float (const char*, float)>& get);
// Display text for a frequency (150 Hz, 2.5 kHz).
juce::String hzText (float hz);
}
