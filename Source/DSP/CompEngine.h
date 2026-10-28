#pragma once

#include <array>
#include <cmath>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"

namespace pc::dsp
{
// Soft knee: how far a level (dB) is above a threshold, rounded over `knee` dB around it.
inline float overshoot (float over, float knee)
{
    if (over <= -0.5f * knee) return 0.0f;
    if (over >= 0.5f * knee) return over;
    const float t = over + 0.5f * knee;
    return t * t / (2.0f * knee);
}

inline float dbToGain (float db) { return std::exp (db * 0.115129255f); } // 10^(db/20)

inline constexpr float kneeDb = 6.0f;
// In most music the higher bands are quieter, so the same COMPRESS reaches further down there.
inline constexpr float thresholdRange[numBands] = { 42.0f, 48.0f, 54.0f };
// AUTO GAIN gives back the reduction a band at this (typical) level gets: COMPRESS does not just turn it down.
inline constexpr float referenceLevel[numBands] = { -12.0f, -15.0f, -21.0f };
// UPWARD lifts what stays below these levels, never anything below the floor (silence, noise).
inline constexpr float upThreshold[numBands] = { -30.0f, -33.0f, -38.0f };
inline constexpr float floorDb = -72.0f, floorFadeDb = 12.0f;
// Time constants per band: the low band is slower so the compressor does not follow (distort) bass waves.
inline constexpr float attackScale[numBands] = { 2.0f, 1.0f, 0.6f };
inline constexpr float releaseScale[numBands] = { 2.0f, 1.0f, 0.7f };
// ANALOGUE: warm soft saturation of LOW and MID (tanh drive); HIGH stays clean.
inline constexpr float analogDrive[numBands] = { 1.2f, 1.0f, 0.0f };

// What COMPRESS (0..1) and UPWARD (0..1) mean for one band, in dB.
struct BandCurve
{
    float threshold = 0.0f, ratio = 1.0f;                     // downward
    float upThreshold = -30.0f, upRatio = 1.0f, upMax = 0.0f; // upward
    float makeup = 0.0f;                                      // AUTO GAIN
};

inline BandCurve bandCurve (int band, float amount, float upward, float autoGain)
{
    BandCurve c;
    c.threshold = -thresholdRange[band] * amount;
    c.ratio = 1.0f + 3.0f * amount + 4.0f * amount * amount; // 1:1 .. 8:1, gentle in the first half
    c.upThreshold = upThreshold[band];
    c.upRatio = 1.0f + 2.0f * upward;                          // 1:1 .. 3:1 below the upward threshold
    c.upMax = 18.0f * upward;
    c.makeup = autoGain * overshoot (referenceLevel[band] - c.threshold, kneeDb) * (1.0f - 1.0f / c.ratio);
    return c;
}

struct GainDb { float down = 0.0f, up = 0.0f; };

inline GainDb staticGain (const BandCurve& c, float levelDb)
{
    GainDb g;
    g.down = -overshoot (levelDb - c.threshold, kneeDb) * (1.0f - 1.0f / c.ratio);
    const float up = std::min (c.upMax, overshoot (c.upThreshold - levelDb, kneeDb) * (1.0f - 1.0f / c.upRatio));
    g.up = up * juce::jlimit (0.0f, 1.0f, (levelDb - floorDb) / floorFadeDb);
    return g;
}

// SPEED 0 (slow: the attack of a hit passes, more punch) .. 1 (fast: denser), for the mid band.
inline float attackMs (float speed) { return 30.0f * std::pow (1.0f / 30.0f, speed); } // 30 .. 1 ms
inline float releaseMs (float speed) { return 500.0f * std::pow (0.1f, speed); }      // 500 .. 50 ms
// How far the start of a hit may pass above the fully compressed level while the slow attack catches up. Without
// this bound AUTO GAIN was added to uncompressed hits: kicks came out up to +13 dBFS (found 2026-10-28, tests).
inline float punchDb (float speed) { return 2.0f + 4.0f * (1.0f - speed); }           // 6 .. 2 dB

//==============================================================================
// Three-band compressor. Linkwitz-Riley 4th-order crossovers (the low band also passes the all-pass of the second
// crossover), so the unprocessed bands add up to the input with a flat response and no latency. Each band: a peak
// detector (stereo-linked by LINK), a soft-knee downward + upward gain curve, AUTO GAIN, its GAIN, optional ANALOGUE
// saturation, BYPASS; MIX blends with the same bands unprocessed (no comb filter), SOLO mutes the other bands.
// Every control is smoothed per sample, so the result does not depend on the block size.
class CompEngine
{
public:
    void prepare (double sampleRate, int maxBlock, const CompParams& p)
    {
        sr = sampleRate;
        const juce::dsp::ProcessSpec spec { sr, (juce::uint32) std::max (1, maxBlock), 2 };
        split1.prepare (spec);
        split2.prepare (spec);
        allpass2.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        allpass2.prepare (spec);
        auto ramp = [this] (auto& s, double seconds) { s.reset (sr, seconds); };
        for (int b = 0; b < numBands; ++b)
        {
            ramp (amount[(size_t) b], 0.02);
            ramp (gainDb[(size_t) b], 0.02);
            ramp (bypassBlend[(size_t) b], 0.01);
            ramp (soloBlend[(size_t) b], 0.01);
        }
        for (auto* s : { &upward, &mix, &outDb, &link, &autoGainBlend })
            ramp (*s, 0.02);
        ramp (analogBlend, 0.03);
        ramp (lowSplit, 0.05);
        ramp (highSplit, 0.05);
        setTargets (p, true);
        currentF1 = currentF2 = -1.0f;
        reset();
    }

    void reset()
    {
        split1.reset();
        split2.reset();
        allpass2.reset();
        for (auto& band : env)
            band.fill (0.0f);
        for (auto& band : envFast)
            band.fill (0.0f);
    }

    void process (float* const* channels, int numChannels, int numSamples, const CompParams& p)
    {
        const int nc = juce::jlimit (0, 2, numChannels);
        blockReductionDb.fill (0.0f);
        blockBoostDb.fill (0.0f);
        if (nc == 0 || numSamples <= 0)
            return;
        setTargets (p, false);
        std::array<float, numBands> att {}, rel {};
        for (size_t b = 0; b < (size_t) numBands; ++b)
        {
            att[b] = coefficient (attackMs (p.speed) * attackScale[b]);
            rel[b] = coefficient (releaseMs (p.speed) * releaseScale[b]);
        }
        const float punch = punchDb (p.speed);

        for (int i = 0; i < numSamples; ++i)
        {
            const float f1 = lowSplit.getNextValue(), f2 = highSplit.getNextValue();
            if (! juce::exactlyEqual (f1, currentF1))
            {
                split1.setCutoffFrequency (f1);
                currentF1 = f1;
            }
            if (! juce::exactlyEqual (f2, currentF2))
            {
                split2.setCutoffFrequency (f2);
                allpass2.setCutoffFrequency (f2);
                currentF2 = f2;
            }
            const float up = upward.getNextValue(), mx = mix.getNextValue(), lk = link.getNextValue();
            const float an = analogBlend.getNextValue(), ag = autoGainBlend.getNextValue();
            const float outGain = dbToGain (outDb.getNextValue());

            float band[numBands][2] {};
            for (int c = 0; c < nc; ++c)
            {
                float lo = 0.0f, rest = 0.0f, mid = 0.0f, top = 0.0f;
                split1.processSample (c, channels[c][i], lo, rest);
                split2.processSample (c, rest, mid, top);
                band[0][c] = allpass2.processSample (c, lo);
                band[1][c] = mid;
                band[2][c] = top;
            }

            float acc[2] {};
            for (int b = 0; b < numBands; ++b)
            {
                const auto bi = (size_t) b;
                const BandCurve curve = bandCurve (b, amount[bi].getNextValue(), up, ag);
                const float bandGain = gainDb[bi].getNextValue(), byp = bypassBlend[bi].getNextValue(), solo = soloBlend[bi].getNextValue();
                const float both = nc > 1 ? std::max (std::abs (band[b][0]), std::abs (band[b][1])) : std::abs (band[b][0]);
                for (int c = 0; c < nc; ++c)
                {
                    const float x = band[b][c];
                    const float d = lk * both + (1.0f - lk) * std::abs (x);
                    // Two detectors with the same release: the SPEED attack (the character) and an instant one
                    // that bounds the pass-through of a hit (punch) to punchDb.
                    float& e = env[bi][(size_t) c];
                    e = d + (d > e ? att[bi] : rel[bi]) * (e - d);
                    float& ef = envFast[bi][(size_t) c];
                    ef = d > ef ? d : d + rel[bi] * (ef - d);
                    const GainDb g = staticGain (curve, juce::Decibels::gainToDecibels (e, -150.0f));
                    const GainDb gf = staticGain (curve, juce::Decibels::gainToDecibels (ef, -150.0f));
                    const float dynamic = std::min (g.down + g.up, gf.down + gf.up + punch);
                    blockReductionDb[bi] = std::max (blockReductionDb[bi], -dynamic);
                    blockBoostDb[bi] = std::max (blockBoostDb[bi], dynamic);
                    float wet = x * dbToGain (dynamic + curve.makeup + bandGain);
                    if (an > 0.0f && analogDrive[b] > 0.0f)
                        wet += an * (std::tanh (analogDrive[b] * wet) / analogDrive[b] - wet);
                    wet += byp * (x - wet);
                    acc[c] += solo * (x + mx * (wet - x));
                }
            }
            for (int c = 0; c < nc; ++c)
                channels[c][i] = acc[c] * outGain;
        }
        split1.snapToZero();
        split2.snapToZero();
        allpass2.snapToZero();
    }

    static constexpr int getLatencySamples() { return 0; }

    // Largest reduction (positive dB) and boost of each band in the last block, for the meters.
    std::array<float, numBands> blockReductionDb {}, blockBoostDb {};

private:
    float coefficient (float ms) const { return (float) std::exp (-1.0 / (0.001 * ms * sr)); }

    void setTargets (const CompParams& p, bool now)
    {
        auto set = [now] (auto& s, float v) { if (now) s.setCurrentAndTargetValue (v); else s.setTargetValue (v); };
        bool anySolo = false;
        for (bool s : p.solo)
            anySolo = anySolo || s;
        for (size_t b = 0; b < (size_t) numBands; ++b)
        {
            set (amount[b], p.comp[b]);
            set (gainDb[b], p.gainDb[b]);
            set (bypassBlend[b], p.bypass[b] ? 1.0f : 0.0f);
            set (soloBlend[b], ! anySolo || p.solo[b] ? 1.0f : 0.0f);
        }
        set (upward, p.upward);
        set (mix, p.mix);
        set (outDb, p.outputDb);
        set (link, p.link);
        set (autoGainBlend, p.autoGain ? 1.0f : 0.0f);
        set (analogBlend, p.analog ? 1.0f : 0.0f);
        const float nyquistGuard = (float) (0.45 * sr);
        set (lowSplit, juce::jlimit (lowSplitMin, lowSplitMax, p.lowSplitHz));
        set (highSplit, juce::jlimit (highSplitMin, std::min (highSplitMax, nyquistGuard), p.highSplitHz));
    }

    double sr = 48000.0;
    juce::dsp::LinkwitzRileyFilter<float> split1, split2, allpass2;
    float currentF1 = -1.0f, currentF2 = -1.0f;
    std::array<std::array<float, 2>, numBands> env {}, envFast {};
    std::array<juce::SmoothedValue<float>, numBands> amount, gainDb, bypassBlend, soloBlend;
    juce::SmoothedValue<float> upward, mix, outDb, link, autoGainBlend, analogBlend;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowSplit, highSplit;
};
}
