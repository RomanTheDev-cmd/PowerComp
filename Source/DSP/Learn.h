#pragma once

#include <algorithm>
#include <array>
#include <vector>
#include "CompEngine.h"

// LEARN (2026-10-28, Roman: "an AI mode like sonible: it listens to the track and sets the controls, while manual
// control and presets stay"). Deterministic, no network: the captured audio is measured (spectrum, attacks, crest
// factor), a profile gives the aim, and COMPRESS of every band is found by running the real CompEngine on the capture
// until the loud parts are reduced by the aim. OUTPUT then matches the loudness, so BEFORE | LEARNED compare fairly.
// Runs off the audio thread (it allocates).
namespace pc::learn
{
enum class Profile { automatic = 0, drums, bass, synth, pad, bus };
inline const juce::StringArray& profileNames()
{
    static const juce::StringArray n { "AUTO", "DRUMS", "BASS", "SYNTH", "PAD", "BUS" };
    return n;
}
inline constexpr double minSeconds = 2.0, maxSeconds = 20.0;

struct Features
{
    float activeSeconds = 0.0f, onsetsPerSecond = 0.0f, crestDb = 0.0f;
    float lowShare = 0.0f, midShare = 0.0f, highShare = 0.0f; // energy below 200 Hz, 200 Hz..5 kHz, above 5 kHz
    float midFlatnessDb = -30.0f;                             // 200 Hz..5 kHz: tonal (low) .. noise (near 0)
    float bassHz = 0.0f;                                      // strongest low frequency (30..200 Hz)
};

struct Suggestion
{
    bool valid = false;
    Profile profile = Profile::automatic;                     // the profile used
    CompParams params;                                        // the full setting to apply
    std::array<float, numBands> targetDb {}, reductionDb {};  // aimed and reached reduction of the loud parts
    Features features;
};

// What each profile aims at with AMOUNT 50 %: reduction of the loud parts per band (dB), SPEED, UPWARD, MIX, splits.
struct Aim { std::array<float, numBands> reductionDb; float speed, upward, mix, lowSplitHz, highSplitHz; };
inline Aim aimFor (Profile p)
{
    switch (p)
    {
        case Profile::drums: return { { 4.0f, 5.0f, 4.0f }, 0.30f, 0.15f, 0.85f, 120.0f, 5000.0f }; // punch, parallel
        case Profile::bass:  return { { 5.0f, 4.0f, 3.0f }, 0.55f, 0.00f, 1.00f, 120.0f, 1500.0f }; // even low end
        case Profile::synth: return { { 3.0f, 4.0f, 4.0f }, 0.50f, 0.10f, 1.00f, 200.0f, 3000.0f };
        case Profile::pad:   return { { 3.0f, 3.0f, 3.0f }, 0.30f, 0.25f, 1.00f, 250.0f, 4000.0f }; // slow, lifts tails
        case Profile::bus:   return { { 2.5f, 2.5f, 2.5f }, 0.45f, 0.05f, 1.00f, 150.0f, 2500.0f }; // glue
        case Profile::automatic: break;
    }
    return { { 3.0f, 3.0f, 3.0f }, 0.45f, 0.05f, 1.00f, 150.0f, 2500.0f };
}

namespace detail
{
inline float dB (double power) { return (float) (10.0 * std::log10 (std::max (power, 1.0e-20))); }

// Mean square of the mid signal in 10 ms frames.
inline std::vector<float> frameLevels (const float* l, const float* r, int n, double sr)
{
    const int hop = std::max (1, (int) (0.01 * sr));
    std::vector<float> out;
    for (int pos = 0; pos + hop <= n; pos += hop)
    {
        double e = 0.0;
        for (int i = pos; i < pos + hop; ++i) { const double m = 0.5 * (l[i] + r[i]); e += m * m; }
        out.push_back (dB (e / hop));
    }
    return out;
}
}

inline Features measure (const float* l, const float* r, int n, double sr)
{
    Features f;
    const auto lv = detail::frameLevels (l, r, n, sr);
    if (lv.empty())
        return f;
    const float top = *std::max_element (lv.begin(), lv.end());
    if (top < -70.0f)
        return f;
    const int hop = std::max (1, (int) (0.01 * sr));
    std::vector<int> active;
    for (int k = 0; k < (int) lv.size(); ++k)
        if (lv[(size_t) k] >= std::max (top - 40.0f, -70.0f)) active.push_back (k);
    f.activeSeconds = (float) active.size() * 0.01f;
    // Attacks: a rise of 6 dB within 30 ms in the loud part, at most one per 50 ms.
    int onsets = 0, last = -100;
    for (int k = 3; k < (int) lv.size(); ++k)
    {
        const float before = std::min ({ lv[(size_t) k - 1], lv[(size_t) k - 2], lv[(size_t) k - 3] });
        if (lv[(size_t) k] >= top - 25.0f && lv[(size_t) k] - before >= 6.0f && k - last >= 5) { ++onsets; last = k; }
    }
    f.onsetsPerSecond = f.activeSeconds > 0.0f ? (float) onsets / f.activeSeconds : 0.0f;
    double peak = 0.0, e = 0.0;
    for (int k : active)
        for (int i = k * hop; i < (k + 1) * hop; ++i)
        {
            const double m = 0.5 * (l[i] + r[i]);
            peak = std::max (peak, std::abs (m));
            e += m * m;
        }
    f.crestDb = (float) (20.0 * std::log10 (std::max (peak, 1.0e-9)) - 10.0 * std::log10 (std::max (e / ((double) active.size() * hop), 1.0e-20)));

    // Average power spectrum of the loud part (Hann, 4096 points, half overlap).
    constexpr int order = 12, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) size * 2), win ((size_t) size);
    std::vector<double> power ((size_t) size / 2 + 1, 0.0);
    for (int i = 0; i < size; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) size);
    int frames = 0;
    for (int pos = 0; pos + size <= n; pos += size / 2)
    {
        const int k = pos / hop + size / (2 * hop);
        if (k >= (int) lv.size() || lv[(size_t) k] < top - 30.0f) continue;
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < size; ++i) buf[(size_t) i] = 0.5f * (l[pos + i] + r[pos + i]) * win[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int i = 0; i <= size / 2; ++i) power[(size_t) i] += (double) buf[(size_t) i] * buf[(size_t) i];
        ++frames;
    }
    if (frames == 0)
        return f;
    const double binHz = sr / size;
    double lo = 0.0, mid = 0.0, hi = 0.0, logSum = 0.0, linSum = 0.0, bassBest = 0.0;
    int midBins = 0;
    for (int i = 1; i <= size / 2; ++i)
    {
        const double hz = i * binHz, p = power[(size_t) i];
        if (hz < 20.0 || hz > 20000.0) continue;
        if (hz < 200.0) lo += p; else if (hz < 5000.0) mid += p; else hi += p;
        if (hz >= 200.0 && hz < 5000.0) { logSum += std::log (p + 1.0e-20); linSum += p; ++midBins; }
        if (hz >= 30.0 && hz <= 200.0 && p > bassBest) { bassBest = p; f.bassHz = (float) hz; }
    }
    const double total = std::max (lo + mid + hi, 1.0e-20);
    f.lowShare = (float) (lo / total);
    f.midShare = (float) (mid / total);
    f.highShare = (float) (hi / total);
    if (midBins > 0 && linSum > 0.0)
        f.midFlatnessDb = (float) (10.0 * std::log10 (std::exp (logSum / midBins) / (linSum / midBins)));
    return f;
}

// AUTO does not guess a genre: on real TSOHT loops (2026-10-28) a guess from spectrum, attacks and crest factor was
// right for 0..83 % of drums, bass, leads and full mixes - Schranz loops are squashed and low-heavy alike. Instead
// AUTO adapts its aim to the material: little extra reduction on what is already squashed (low crest factor), more
// on dynamic material; SPEED, splits and level follow the measurement as with every profile.
inline Aim autoAim (const Features& f)
{
    Aim a = aimFor (Profile::automatic);
    const float r = juce::jlimit (1.5f, 5.0f, 0.5f * (f.crestDb - 6.0f));
    a.reductionDb = { r, r, r };
    return a;
}

// The reduction of the loud parts each band gets with `p`: the 90th percentile of each band's largest reduction per
// 10 ms block, over the blocks within 20 dB of the loudest. `out` (optional) receives the processed audio.
inline std::array<float, numBands> loudReduction (const float* l, const float* r, int n, double sr, const CompParams& p,
                                                  std::vector<float>* outL = nullptr, std::vector<float>* outR = nullptr)
{
    const int hop = std::max (1, (int) (0.01 * sr));
    const auto lv = detail::frameLevels (l, r, n, sr);
    dsp::CompEngine e;
    e.prepare (sr, hop, p);
    std::vector<float> bl ((size_t) hop), br ((size_t) hop);
    std::array<std::vector<float>, numBands> red;
    const float top = lv.empty() ? 0.0f : *std::max_element (lv.begin(), lv.end());
    if (outL != nullptr) { outL->assign ((size_t) n, 0.0f); outR->assign ((size_t) n, 0.0f); }
    for (int k = 0, pos = 0; pos + hop <= n; ++k, pos += hop)
    {
        std::copy (l + pos, l + pos + hop, bl.begin());
        std::copy (r + pos, r + pos + hop, br.begin());
        float* ch[2] = { bl.data(), br.data() };
        e.process (ch, 2, hop, p);
        if (outL != nullptr) { std::copy (bl.begin(), bl.end(), outL->begin() + pos); std::copy (br.begin(), br.end(), outR->begin() + pos); }
        if (lv[(size_t) k] >= top - 20.0f)
            for (int b = 0; b < numBands; ++b) red[(size_t) b].push_back (e.blockReductionDb[(size_t) b]);
    }
    std::array<float, numBands> res {};
    for (int b = 0; b < numBands; ++b)
    {
        auto& v = red[(size_t) b];
        std::sort (v.begin(), v.end());
        res[(size_t) b] = v.empty() ? 0.0f : v[(size_t) (0.9 * (double) (v.size() - 1))];
    }
    return res;
}

// RMS (dB, both channels) of band `band` of the capture processed with `p` (SOLO isolates the band, OUTPUT and the
// band GAIN left out).
inline float bandLevelDb (const float* l, const float* r, int n, double sr, CompParams p, int band)
{
    p.solo = { band == 0, band == 1, band == 2 };
    p.bypass = { false, false, false };
    p.gainDb = { 0.0f, 0.0f, 0.0f };
    p.outputDb = 0.0f;
    std::vector<float> a (l, l + n), b (r, r + n);
    dsp::CompEngine e;
    e.prepare (sr, 512, p);
    for (int pos = 0; pos < n; pos += 512)
    {
        float* ch[2] = { a.data() + pos, b.data() + pos };
        e.process (ch, 2, std::min (512, n - pos), p);
    }
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += (double) a[(size_t) i] * a[(size_t) i] + (double) b[(size_t) i] * b[(size_t) i];
    return detail::dB (sum / (2.0 * n));
}

// Listens to `n` samples of stereo audio and suggests the setting. `current` keeps the instance settings (DIGITAL |
// ANALOGUE, AUTO GAIN, STEREO LINK). Invalid when there is less than minSeconds of sound.
inline Suggestion analyse (const float* l, const float* r, int n, double sr, Profile profile, float amount, const CompParams& current)
{
    Suggestion s;
    if (l == nullptr || r == nullptr || n <= 0 || sr <= 0.0)
        return s;
    s.features = measure (l, r, n, sr);
    if (s.features.activeSeconds < (float) minSeconds)
        return s;
    s.profile = profile;
    const Aim aim = profile == Profile::automatic ? autoAim (s.features) : aimFor (profile);
    amount = juce::jlimit (0.0f, 1.0f, amount);

    CompParams p = current;
    p.solo = { false, false, false };
    p.bypass = { false, false, false };
    p.gainDb = { 0.0f, 0.0f, 0.0f };
    p.outputDb = 0.0f;
    p.upward = 0.0f;
    p.mix = 1.0f;
    // LOW|MID: above the fundamental for low-end material; MID|HIGH lower when there is almost nothing up there.
    p.lowSplitHz = aim.lowSplitHz;
    const bool lowEnd = s.profile == Profile::bass || s.profile == Profile::drums
                        || (s.profile == Profile::automatic && s.features.lowShare >= 0.3f);
    if (lowEnd && s.features.bassHz > 0.0f)
        p.lowSplitHz = juce::jlimit (60.0f, 250.0f, 2.5f * s.features.bassHz);
    p.highSplitHz = aim.highSplitHz;
    if (s.features.highShare < 0.0005f) // almost nothing above 5 kHz (-33 dB)
        p.highSplitHz = std::min (aim.highSplitHz, 1500.0f);
    p.highSplitHz = juce::jlimit (std::max (highSplitMin, 3.0f * p.lowSplitHz), highSplitMax, p.highSplitHz);
    p.lowSplitHz = juce::jlimit (lowSplitMin, lowSplitMax, p.lowSplitHz);
    const float density = juce::jlimit (-1.0f, 1.0f, (s.features.onsetsPerSecond - 4.0f) / 6.0f);
    p.speed = juce::jlimit (0.0f, 1.0f, aim.speed + 0.1f * density);

    for (int b = 0; b < numBands; ++b)
        s.targetDb[(size_t) b] = aim.reductionDb[(size_t) b] * 2.0f * amount;
    // COMPRESS per band by bisection: the bands are detected independently, so one pass serves all three.
    std::array<float, numBands> lo {}, hi { 1.0f, 1.0f, 1.0f };
    for (int it = 0; it < 9; ++it)
    {
        for (int b = 0; b < numBands; ++b) p.comp[(size_t) b] = 0.5f * (lo[(size_t) b] + hi[(size_t) b]);
        const auto red = loudReduction (l, r, n, sr, p);
        for (int b = 0; b < numBands; ++b)
            (red[(size_t) b] < s.targetDb[(size_t) b] ? lo : hi)[(size_t) b] = p.comp[(size_t) b];
    }
    for (int b = 0; b < numBands; ++b)
    {
        const auto bi = (size_t) b;
        p.comp[bi] = s.targetDb[bi] < 0.05f ? 0.0f : (hi[bi] >= 1.0f && lo[bi] > 0.99f ? 1.0f : 0.5f * (lo[bi] + hi[bi]));
    }
    p.upward = juce::jlimit (0.0f, 1.0f, aim.upward * 2.0f * amount);
    p.mix = aim.mix;
    // The tone stays: each band's GAIN brings it back to its own level (RMS), so LEARN changes the dynamics, not the
    // balance. AUTO GAIN alone compensates for a typical band level and tilted quiet material by many dB (tests).
    {
        CompParams dry = p;
        dry.comp = { 0.0f, 0.0f, 0.0f };
        dry.upward = 0.0f;
        dry.mix = 1.0f;
        std::array<float, numBands> in {}, change {};
        float loudest = -200.0f;
        for (int b = 0; b < numBands; ++b)
        {
            in[(size_t) b] = bandLevelDb (l, r, n, sr, dry, b);
            change[(size_t) b] = bandLevelDb (l, r, n, sr, p, b) - in[(size_t) b];
            loudest = std::max (loudest, in[(size_t) b]);
        }
        // The part common to all bands goes to OUTPUT (below), the differences to the band GAINs (+-12 dB).
        double w = 0.0, common = 0.0;
        for (int b = 0; b < numBands; ++b)
            if (in[(size_t) b] >= loudest - 40.0f)
            {
                const double e = std::pow (10.0, in[(size_t) b] / 10.0);
                w += e;
                common += e * change[(size_t) b];
            }
        common = w > 0.0 ? common / w : 0.0;
        for (int b = 0; b < numBands; ++b)
            p.gainDb[(size_t) b] = in[(size_t) b] < loudest - 40.0f ? 0.0f
                                   : juce::jlimit (-12.0f, 12.0f, std::round ((float) (common - change[(size_t) b]) * 10.0f) / 10.0f);
    }
    // OUTPUT: the processed capture as loud (RMS) as the capture.
    std::vector<float> ol, orr;
    s.reductionDb = loudReduction (l, r, n, sr, p, &ol, &orr);
    // A band that even full COMPRESS reduces by less than 1 dB has (almost) nothing in it: leave it alone, or AUTO
    // GAIN would lift the near-empty band by up to +26 dB (the pads of the references, 2026-10-28).
    bool emptied = false;
    for (int b = 0; b < numBands; ++b)
        if (p.comp[(size_t) b] >= 1.0f && s.reductionDb[(size_t) b] < 1.0f) { p.comp[(size_t) b] = 0.0f; emptied = true; }
    if (emptied)
        s.reductionDb = loudReduction (l, r, n, sr, p, &ol, &orr);
    double ein = 0.0, eout = 0.0;
    for (int i = 0; i < n; ++i)
    {
        ein += (double) l[i] * l[i] + (double) r[i] * r[i];
        eout += (double) ol[(size_t) i] * ol[(size_t) i] + (double) orr[(size_t) i] * orr[(size_t) i];
    }
    p.outputDb = juce::jlimit (-24.0f, 12.0f, std::round ((detail::dB (ein) - detail::dB (eout)) * 10.0f) / 10.0f);
    s.params = p;
    s.valid = true;
    return s;
}
}
