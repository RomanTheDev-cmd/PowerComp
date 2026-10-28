// PowerComp engine and preset tests (headless). Run from the project folder.
//   powercomp_tests              all checks
//   powercomp_tests --calibrate  the OUTPUT that makes each factory preset as loud as its input (reference loop)
#include <juce_audio_processors/juce_audio_processors.h>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include "DSP/CompEngine.h"
#include "Presets.h"

using namespace pc;

namespace
{
int checks = 0, failures = 0;
void expect (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "FAIL: " << what << "\n"; }
}
double db (double g) { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }
juce::String f2 (double v) { return juce::String (v, 2); }

using Buf = juce::AudioBuffer<float>;

// Nothing compressed, nothing lifted, unity levels.
CompParams neutral()
{
    CompParams p;
    p.comp = { 0.0f, 0.0f, 0.0f };
    return p;
}

Buf process (const CompParams& p, const Buf& in, double sr, int block = 512,
             const std::function<void (CompParams&, int)>& automate = {})
{
    dsp::CompEngine e;
    e.prepare (sr, block, p);
    Buf out (in);
    const int total = out.getNumSamples(), nc = out.getNumChannels();
    CompParams q = p;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        if (automate) automate (q, pos);
        float* ch[2] = { out.getWritePointer (0, pos), nc > 1 ? out.getWritePointer (1, pos) : nullptr };
        e.process (ch, nc, n, q);
    }
    return out;
}

Buf sine (double f, double dbfs, double seconds, double sr, int channels = 2)
{
    Buf b (channels, (int) (seconds * sr));
    const double a = std::pow (10.0, dbfs / 20.0);
    for (int i = 0; i < b.getNumSamples(); ++i)
        for (int c = 0; c < channels; ++c)
            b.setSample (c, i, (float) (a * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / sr)));
    return b;
}

Buf add (Buf a, const Buf& b)
{
    for (int c = 0; c < a.getNumChannels(); ++c)
        a.addFrom (c, 0, b, c, 0, std::min (a.getNumSamples(), b.getNumSamples()));
    return a;
}

Buf concat (const Buf& a, const Buf& b)
{
    Buf r (a.getNumChannels(), a.getNumSamples() + b.getNumSamples());
    for (int c = 0; c < a.getNumChannels(); ++c)
    {
        r.copyFrom (c, 0, a, c, 0, a.getNumSamples());
        r.copyFrom (c, a.getNumSamples(), b, c, 0, b.getNumSamples());
    }
    return r;
}

// A deterministic techno bar pattern at 130 BPM, 16 beats: kick on the beat, bass on the off-beat, 16th hats
// (slightly louder on the left), a held chord. Peak -3 dBFS.
Buf referenceLoop (double sr, bool kickOnly = false)
{
    const double pi = juce::MathConstants<double>::pi, beat = 60.0 / 130.0;
    const int total = (int) (16 * beat * sr);
    Buf b (2, total);
    juce::Random rng (1234);
    float prevNoise = 0.0f;
    for (int i = 0; i < total; ++i)
    {
        const double t = i / sr, tb = std::fmod (t, beat);
        const double kickPhase = 2.0 * pi * (50.0 * tb + 100.0 * 0.03 * (1.0 - std::exp (-tb / 0.03)));
        const double kick = 0.8 * std::exp (-tb / 0.25) * std::sin (kickPhase);
        double bass = 0.0, hat = 0.0, pad = 0.0;
        if (! kickOnly)
        {
            const double toff = std::fmod (t + beat * 0.5, beat);
            for (int k = 1; k <= 8; ++k)
                bass += std::sin (2.0 * pi * 55.0 * k * t) / k;
            bass *= 0.25 * std::exp (-toff / 0.1);
            const float noise = rng.nextFloat() * 2.0f - 1.0f;
            hat = 0.2 * std::exp (-std::fmod (t, beat * 0.25) / 0.02) * (noise - prevNoise);
            prevNoise = noise;
            pad = 0.05 * (std::sin (2.0 * pi * 220.0 * t) + std::sin (2.0 * pi * 277.18 * t) + std::sin (2.0 * pi * 329.63 * t));
        }
        b.setSample (0, i, (float) (kick + bass + hat + pad));
        b.setSample (1, i, (float) (kick + bass + 0.7 * hat + pad));
    }
    b.applyGain (0.7079f / std::max (b.getMagnitude (0, 0, total), b.getMagnitude (1, 0, total)));
    return b;
}

double rms (const Buf& a, int from = 0, int to = -1)
{
    if (to < 0) to = a.getNumSamples();
    double s = 0.0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        s += std::pow (a.getRMSLevel (c, from, to - from), 2.0);
    return std::sqrt (s / a.getNumChannels());
}
double peak (const Buf& a, int from = 0, int to = -1)
{
    if (to < 0) to = a.getNumSamples();
    double m = 0.0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        m = std::max (m, (double) a.getMagnitude (c, from, to - from));
    return m;
}
// Amplitude of the component at f (single DFT bin over [from, to), channel c).
double amplitude (const Buf& a, double f, double sr, int from, int to, int c = 0)
{
    double re = 0.0, im = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f * i / sr;
        re += a.getSample (c, i) * std::cos (w);
        im += a.getSample (c, i) * std::sin (w);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (to - from);
}
bool finite (const Buf& a)
{
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (! std::isfinite (a.getSample (c, i))) return false;
    return true;
}
// Largest second difference relative to the peak: about (2 pi f / sr)^2 for a clean sine, near 1 for a click.
double roughness (const Buf& a, int from = 2, int to = -1)
{
    if (to < 0) to = a.getNumSamples();
    double m = 0.0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = std::max (2, from); i < to; ++i)
            m = std::max (m, (double) std::abs (a.getSample (c, i) - 2.0f * a.getSample (c, i - 1) + a.getSample (c, i - 2)));
    return m / std::max (1.0e-9, peak (a, from, to));
}

//==============================================================================
// With nothing compressed the three bands add up to the input: no level change at any frequency, for any split.
void testNeutralIsFlat()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (auto [lo, hi] : { std::pair { 150.0f, 2500.0f }, std::pair { 30.0f, 800.0f }, std::pair { 600.0f, 800.0f }, std::pair { 600.0f, 12000.0f } })
        {
            auto p = neutral();
            p.lowSplitHz = lo;
            p.highSplitHz = hi;
            double worst = 0.0;
            for (double f : { 25.0, 40.0, 80.0, 150.0, 300.0, 600.0, 700.0, 800.0, 1500.0, 2500.0, 5000.0, 10000.0, 12000.0, 16000.0 })
            {
                const auto in = sine (f, -12.0, 1.0, sr);
                const auto out = process (p, in, sr);
                const int from = (int) (0.5 * sr), len = (int) (std::floor (0.45 * f) / f * sr);
                worst = std::max (worst, std::abs (db (rms (out, from, from + len) / rms (in, from, from + len))));
            }
            expect (worst < 0.1, "neutral at " + juce::String (sr) + " Hz, splits " + juce::String (lo) + "/" + juce::String (hi)
                                     + ": flat (worst " + f2 (worst) + " dB)");
        }
}

// No lookahead: an impulse comes out from the first sample.
void testNoLatency()
{
    Buf in (2, 4800);
    in.clear();
    in.setSample (0, 0, 0.5f);
    in.setSample (1, 0, 0.5f);
    const auto out = process (CompParams(), in, 48000.0);
    expect (std::abs (out.getSample (0, 0)) > 0.1f, "no latency: the impulse starts at sample 0 (" + f2 (out.getSample (0, 0)) + ")");
}

// COMPRESS narrows the gap between a loud and a quiet part in its band; at 0 the gap stays.
void testCompressionNarrowsRange()
{
    const double sr = 48000.0;
    const double freqs[numBands] = { 100.0, 1000.0, 8000.0 };
    const char* names[numBands] = { "LOW", "MID", "HIGH" };
    for (int b = 0; b < numBands; ++b)
    {
        const auto in = concat (sine (freqs[b], -6.0, 1.5, sr), sine (freqs[b], -26.0, 1.5, sr));
        auto gap = [&] (float amount)
        {
            auto p = neutral();
            p.comp[(size_t) b] = amount;
            const auto out = process (p, in, sr);
            return db (rms (out, (int) (1.2 * sr), (int) (1.5 * sr)) / rms (out, (int) (2.7 * sr), (int) (3.0 * sr)));
        };
        const double off = gap (0.0f), full = gap (1.0f), half = gap (0.5f);
        expect (std::abs (off - 20.0) < 0.3, juce::String (names[b]) + " COMPRESS 0 keeps the 20 dB gap (" + f2 (off) + ")");
        expect (full < 8.0, juce::String (names[b]) + " COMPRESS 100 % narrows the gap below 8 dB (" + f2 (full) + ")");
        expect (half < off - 3.0 && half > full, juce::String (names[b]) + " COMPRESS 50 % is in between (" + f2 (half) + ")");
    }
}

// Compressing one band leaves the others alone.
void testBandsIndependent()
{
    const double sr = 48000.0;
    const auto in = add (sine (60.0, -6.0, 2.0, sr), sine (5000.0, -20.0, 2.0, sr));
    const int from = (int) (1.0 * sr), to = (int) (2.0 * sr);
    const auto ref = process (neutral(), in, sr);
    auto lowOnly = neutral();
    lowOnly.comp[0] = 1.0f;
    const auto a = process (lowOnly, in, sr);
    expect (std::abs (db (amplitude (a, 5000.0, sr, from, to) / amplitude (ref, 5000.0, sr, from, to))) < 0.3,
            "LOW COMPRESS does not change the 5 kHz part");
    expect (std::abs (db (amplitude (a, 60.0, sr, from, to) / amplitude (ref, 60.0, sr, from, to))) > 1.0,
            "LOW COMPRESS acts on the 60 Hz part (" + f2 (db (amplitude (a, 60.0, sr, from, to) / amplitude (ref, 60.0, sr, from, to))) + " dB)");
    auto highOnly = neutral();
    highOnly.comp[2] = 1.0f;
    const auto h = process (highOnly, in, sr);
    expect (std::abs (db (amplitude (h, 60.0, sr, from, to) / amplitude (ref, 60.0, sr, from, to))) < 0.3,
            "HIGH COMPRESS does not change the 60 Hz part");
}

// MIX 0 is the dry signal, whatever else is set (parallel compression without comb filtering).
void testMixZeroIsDry()
{
    const double sr = 48000.0;
    const auto in = referenceLoop (sr);
    auto p = neutral();
    p.comp = { 1.0f, 1.0f, 1.0f };
    p.gainDb = { 6.0f, 6.0f, 6.0f };
    p.upward = 1.0f;
    p.speed = 1.0f;
    p.analog = true;
    p.mix = 0.0f;
    const auto dry = process (neutral(), in, sr), out = process (p, in, sr);
    float worst = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < out.getNumSamples(); ++i)
            worst = std::max (worst, std::abs (out.getSample (c, i) - dry.getSample (c, i)));
    expect (worst < 1.0e-5f, "MIX 0 = the dry signal (largest difference " + juce::String (worst) + ")");
    p.mix = 0.5f;
    const auto halfMix = process (p, in, sr);
    expect (std::abs (db (rms (halfMix) / rms (dry))) > 0.2, "MIX 50 % differs from the dry signal");
}

void testSoloBypassGainOutput()
{
    const double sr = 48000.0;
    const auto in = add (sine (60.0, -12.0, 2.0, sr), sine (5000.0, -12.0, 2.0, sr));
    const int from = (int) (1.0 * sr), to = (int) (2.0 * sr);
    const auto ref = process (neutral(), in, sr);
    auto change = [&] (const Buf& out, double f) { return db (amplitude (out, f, sr, from, to) / amplitude (ref, f, sr, from, to)); };

    auto solo = neutral();
    solo.solo[0] = true;
    const auto s = process (solo, in, sr);
    expect (std::abs (change (s, 60.0)) < 0.5 && change (s, 5000.0) < -60.0,
            "SOLO LOW: only the low band plays (60 Hz " + f2 (change (s, 60.0)) + " dB, 5 kHz " + f2 (change (s, 5000.0)) + " dB)");
    solo.solo[2] = true;
    const auto s2 = process (solo, in, sr);
    expect (std::abs (change (s2, 60.0)) < 0.5 && std::abs (change (s2, 5000.0)) < 0.5, "SOLO LOW + HIGH: both play");

    auto byp = neutral();
    byp.comp[0] = 1.0f;
    byp.gainDb[0] = 6.0f;
    byp.bypass[0] = true;
    const auto bp = process (byp, in, sr);
    expect (std::abs (change (bp, 60.0)) < 0.1, "BYPASS LOW: the low band is untouched (" + f2 (change (bp, 60.0)) + " dB)");

    auto gain = neutral();
    gain.gainDb[2] = 6.0f;
    gain.highSplitHz = 1000.0f; // 5 kHz well inside the high band (an octave above a split the mid band still has -24 dB)
    const auto g = process (gain, in, sr);
    expect (std::abs (change (g, 5000.0) - 6.0) < 0.2 && std::abs (change (g, 60.0)) < 0.2,
            "HIGH GAIN +6 dB raises only the high band (" + f2 (change (g, 5000.0)) + " / " + f2 (change (g, 60.0)) + ")");

    auto out = neutral();
    out.outputDb = -6.0f;
    const auto o = process (out, in, sr);
    expect (std::abs (db (rms (o, from, to) / rms (ref, from, to)) + 6.0) < 0.05, "OUTPUT -6 dB");
}

// UPWARD lifts quiet material, never digital silence or the noise floor.
void testUpward()
{
    const double sr = 48000.0;
    const auto quiet = sine (1000.0, -50.0, 2.0, sr);
    const int from = (int) (1.0 * sr), to = (int) (2.0 * sr);
    auto p = neutral();
    p.upward = 1.0f;
    const double lift = db (rms (process (p, quiet, sr), from, to) / rms (quiet, from, to));
    expect (lift > 6.0, "UPWARD 100 % lifts a -50 dBFS tone by more than 6 dB (" + f2 (lift) + ")");
    const double none = db (rms (process (neutral(), quiet, sr), from, to) / rms (quiet, from, to));
    expect (std::abs (none) < 0.1, "UPWARD 0 leaves it (" + f2 (none) + ")");

    Buf silence (2, (int) sr);
    silence.clear();
    auto all = p;
    all.comp = { 1.0f, 1.0f, 1.0f };
    expect (peak (process (all, silence, sr)) <= 0.0, "digital silence stays silent");
    // (COMPRESS with AUTO GAIN does raise quiet material, noise included: that is compression. UPWARD alone must not.)
    Buf noise (2, (int) (2.0 * sr));
    juce::Random rng (7);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < noise.getNumSamples(); ++i)
            noise.setSample (c, i, 3.0e-5f * (rng.nextFloat() * 2.0f - 1.0f));
    const double floorLift = db (rms (process (p, noise, sr), from, to) / rms (noise, from, to));
    expect (floorLift < 1.0, "the -90 dB noise floor is not lifted (" + f2 (floorLift) + " dB)");
}

// AUTO GAIN: turning COMPRESS up does not simply make the sound quieter.
void testAutoGain()
{
    const double sr = 48000.0;
    const auto in = referenceLoop (sr);
    double worst = 0.0;
    for (float a : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        auto p = neutral();
        p.comp = { a, a, a };
        worst = std::max (worst, std::abs (db (rms (process (p, in, sr)) / rms (in))));
    }
    expect (worst < 2.0, "AUTO GAIN: loudness within 2 dB for any COMPRESS (worst " + f2 (worst) + " dB)");
    auto off = neutral();
    off.comp = { 1.0f, 1.0f, 1.0f };
    off.autoGain = false;
    const double drop = db (rms (process (off, in, sr)) / rms (in));
    expect (drop < -4.0, "without AUTO GAIN full compression is quieter (" + f2 (drop) + " dB)");
}

// SPEED: slow lets the attack of a kick through (more punch), fast holds it down (denser).
void testSpeed()
{
    const double sr = 48000.0;
    const auto in = referenceLoop (sr, true);
    auto crest = [&] (float speed)
    {
        auto p = neutral();
        p.comp = { 0.7f, 0.7f, 0.7f };
        p.speed = speed;
        const auto out = process (p, in, sr);
        return db (peak (out) / rms (out));
    };
    const double slow = crest (0.0f), fast = crest (1.0f);
    expect (slow > fast + 1.0, "SPEED: slow keeps more punch than fast (crest " + f2 (slow) + " / " + f2 (fast) + " dB)");

    // The slow attack lets a hit through, but AUTO GAIN must not be added on top of it uncompressed
    // (before the bound kicks came out at up to +13 dBFS).
    double worst = -100.0;
    for (float speed : { 0.0f, 0.5f, 1.0f })
        for (float amount : { 0.5f, 1.0f })
        {
            auto p = neutral();
            p.comp = { amount, amount, amount };
            p.speed = speed;
            p.upward = amount > 0.9f ? 1.0f : 0.0f;
            worst = std::max (worst, db (peak (process (p, in, sr)) / peak (in)));
        }
    expect (worst < 3.0, "a kick's peak rises at most 3 dB at any SPEED and COMPRESS (worst +" + f2 (worst) + " dB)");
}

void testStereo()
{
    const double sr = 48000.0;
    Buf in (2, (int) (2.0 * sr));
    const auto loud = sine (100.0, -6.0, 2.0, sr, 1), quiet = sine (100.0, -30.0, 2.0, sr, 1);
    in.copyFrom (0, 0, loud, 0, 0, in.getNumSamples());
    in.copyFrom (1, 0, quiet, 0, 0, in.getNumSamples());
    const int from = (int) (1.0 * sr), to = (int) (2.0 * sr);
    auto gains = [&] (float link)
    {
        auto p = neutral();
        p.comp[0] = 0.5f;
        p.link = link;
        const auto out = process (p, in, sr);
        return std::pair { db (amplitude (out, 100.0, sr, from, to, 0) / amplitude (in, 100.0, sr, from, to, 0)),
                           db (amplitude (out, 100.0, sr, from, to, 1) / amplitude (in, 100.0, sr, from, to, 1)) };
    };
    const auto linked = gains (1.0f), free = gains (0.0f);
    expect (std::abs (linked.first - linked.second) < 0.5, "STEREO LINK 100 %: both sides get the same gain (" + f2 (linked.first) + " / " + f2 (linked.second) + ")");
    expect (free.second - free.first > 3.0, "STEREO LINK 0: each side on its own (" + f2 (free.first) + " / " + f2 (free.second) + ")");

    // The same signal on both sides stays the same on both sides; a mono bus gives the same result.
    const auto loop = referenceLoop (sr);
    Buf twin (2, loop.getNumSamples());
    twin.copyFrom (0, 0, loop, 0, 0, loop.getNumSamples());
    twin.copyFrom (1, 0, loop, 0, 0, loop.getNumSamples());
    auto p = CompParams();
    p.analog = true;
    p.upward = 0.4f;
    const auto out = process (p, twin, sr);
    float diff = 0.0f;
    for (int i = 0; i < out.getNumSamples(); ++i)
        diff = std::max (diff, std::abs (out.getSample (0, i) - out.getSample (1, i)));
    expect (diff == 0.0f, "identical sides stay identical (" + juce::String (diff) + ")");
    Buf mono (1, loop.getNumSamples());
    mono.copyFrom (0, 0, loop, 0, 0, loop.getNumSamples());
    const auto mout = process (p, mono, sr);
    float mdiff = 0.0f;
    for (int i = 0; i < mout.getNumSamples(); ++i)
        mdiff = std::max (mdiff, std::abs (mout.getSample (0, i) - out.getSample (0, i)));
    expect (mdiff < 1.0e-6f, "a mono bus sounds like one side of stereo (" + juce::String (mdiff) + ")");
}

// ANALOGUE: warm harmonics on loud low and mid parts, almost nothing on quiet ones, the same loudness, no click.
void testAnalogue()
{
    const double sr = 48000.0;
    const int from = (int) (0.5 * sr), to = (int) (1.5 * sr);
    auto third = [&] (double level, bool analog)
    {
        auto p = neutral();
        p.analog = analog;
        const auto out = process (p, sine (60.0, level, 1.5, sr), sr);
        return db (amplitude (out, 180.0, sr, from, to) / amplitude (out, 60.0, sr, from, to));
    };
    const double hot = third (-6.0, true), clean = third (-6.0, false), soft = third (-36.0, true);
    expect (hot > -45.0, "ANALOGUE adds harmonics to a loud bass (3rd " + f2 (hot) + " dB)");
    expect (clean < -80.0, "DIGITAL adds none (3rd " + f2 (clean) + " dB)");
    expect (soft < -60.0, "ANALOGUE leaves a quiet bass almost clean (3rd " + f2 (soft) + " dB)");

    const auto loop = referenceLoop (sr);
    auto a = CompParams();
    a.analog = true;
    const double level = db (rms (process (a, loop, sr)) / rms (process (CompParams(), loop, sr)));
    expect (std::abs (level) < 1.5, "ANALOGUE keeps the loudness (" + f2 (level) + " dB)");

    const auto tone = sine (60.0, -6.0, 1.0, sr);
    const auto switched = process (neutral(), tone, sr, 512, [] (CompParams& q, int pos) { q.analog = pos >= 24000; });
    const double r = roughness (switched, (int) (0.4 * sr), (int) (0.7 * sr));
    expect (r < 0.01, "switching DIGITAL -> ANALOGUE does not click (" + juce::String (r, 4) + ")");
}

// Every control can jump during playback without a click.
void testAutomationIsSmooth()
{
    const double sr = 48000.0;
    const auto in = add (sine (300.0, -10.0, 1.0, sr), sine (3000.0, -22.0, 1.0, sr));
    const double clean = roughness (process (neutral(), in, sr));
    using Set = std::function<void (CompParams&)>;
    struct Jump { const char* name; Set start, set; };
    const Set none = [] (CompParams&) {};
    const Set squeezed = [] (CompParams& q) { q.comp = { 1.0f, 1.0f, 1.0f }; };
    const std::vector<Jump> jumps {
        { "COMPRESS 0 -> 100 %", none, squeezed },
        { "GAIN -12 -> +12 dB", [] (CompParams& q) { q.gainDb = { -12.0f, -12.0f, -12.0f }; }, [] (CompParams& q) { q.gainDb = { 12.0f, 12.0f, 12.0f }; } },
        { "OUTPUT -24 -> +12 dB", [] (CompParams& q) { q.outputDb = -24.0f; }, [] (CompParams& q) { q.outputDb = 12.0f; } },
        { "MIX 0 -> 100 %", [] (CompParams& q) { q.comp = { 1.0f, 1.0f, 1.0f }; q.gainDb = { -12.0f, -12.0f, -12.0f }; q.mix = 0.0f; },
          [] (CompParams& q) { q.mix = 1.0f; } },
        { "UPWARD 0 -> 100 %", none, [] (CompParams& q) { q.upward = 1.0f; } },
        { "splits to the other end", [] (CompParams& q) { q.lowSplitHz = 30.0f; q.highSplitHz = 800.0f; },
          [] (CompParams& q) { q.lowSplitHz = 600.0f; q.highSplitHz = 12000.0f; } },
        { "SOLO MID", none, [] (CompParams& q) { q.solo[1] = true; } },
        { "BYPASS LOW", squeezed, [] (CompParams& q) { q.bypass[0] = true; } },
        { "AUTO GAIN off", squeezed, [] (CompParams& q) { q.autoGain = false; } },
        { "SPEED and LINK", squeezed, [] (CompParams& q) { q.speed = 1.0f; q.link = 0.0f; } },
    };
    for (const auto& j : jumps)
    {
        auto start = neutral();
        j.start (start);
        const auto out = process (start, in, sr, 512, [&j] (CompParams& q, int pos) { if (pos == 512 * 40) j.set (q); });
        const double r = roughness (out, (int) (0.3 * sr));
        expect (r < std::max (0.02, clean * 2.0), juce::String ("no click on ") + j.name + " (" + juce::String (r, 4) + ", clean " + juce::String (clean, 4) + ")");
    }
}

// The result does not depend on how the host cuts the audio into blocks.
void testBlockSizes()
{
    const double sr = 48000.0;
    const auto in = referenceLoop (sr);
    auto p = CompParams();
    p.analog = true;
    p.upward = 0.3f;
    p.comp = { 0.6f, 0.4f, 0.5f };
    const auto ref = process (p, in, sr, 512);
    for (int block : { 1, 37, 64, 1000 })
    {
        const auto out = process (p, in, sr, block);
        float worst = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < out.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (out.getSample (c, i) - ref.getSample (c, i)));
        expect (worst < 1.0e-6f, "block size " + juce::String (block) + " = block size 512 (" + juce::String (worst) + ")");
    }
}

// Other sample rates behave like 48 kHz.
void testSampleRates()
{
    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        const auto in = referenceLoop (sr);
        auto p = neutral();
        p.comp = { 0.8f, 0.8f, 0.8f };
        const double here = db (rms (process (p, in, sr)) / rms (in));
        const auto in48 = referenceLoop (48000.0);
        const double at48 = db (rms (process (p, in48, 48000.0)) / rms (in48));
        expect (std::abs (here - at48) < 0.5, juce::String (sr) + " Hz compresses like 48 kHz (" + f2 (here) + " / " + f2 (at48) + " dB)");
    }
}

void testExtremes()
{
    const double sr = 48000.0;
    Buf noise (2, (int) (2.0 * sr));
    juce::Random rng (3);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < noise.getNumSamples(); ++i)
            noise.setSample (c, i, 4.0f * (rng.nextFloat() * 2.0f - 1.0f));
    CompParams p;
    p.comp = { 1.0f, 1.0f, 1.0f };
    p.gainDb = { 12.0f, 12.0f, 12.0f };
    p.upward = 1.0f;
    p.outputDb = 12.0f;
    p.speed = 1.0f;
    p.analog = true;
    p.lowSplitHz = 600.0f;
    p.highSplitHz = 800.0f;
    const auto out = process (p, noise, sr);
    expect (finite (out) && peak (out) < 100.0, "+12 dBFS noise with every control at its end: finite (peak " + f2 (db (peak (out))) + " dBFS)");
}

void testPresets()
{
    const double sr = 48000.0;
    const auto in = referenceLoop (sr);
    const auto& all = factoryPresets();
    expect (all.size() >= 16, "at least 16 factory presets (" + juce::String ((int) all.size()) + ")");
    int bad = 0;
    double worstLevel = 0.0, worstPeak = -100.0;
    for (const auto& pr : all)
    {
        const auto out = process (paramsFromSnapshot (pr.values), in, sr);
        const double level = db (rms (out) / rms (in));
        worstLevel = std::max (worstLevel, std::abs (level));
        worstPeak = std::max (worstPeak, db (peak (out)));
        if (! finite (out) || std::abs (level) > 1.0)
        {
            ++bad;
            std::cout << "  preset " << pr.name << ": level " << level << " dB\n";
        }
    }
    expect (bad == 0, "every preset finite and as loud as its input within 1 dB (worst " + f2 (worstLevel) + " dB)");
    expect (worstPeak < 0.0, "no preset goes over 0 dBFS on the reference loop (worst " + f2 (worstPeak) + " dBFS)");
    const auto first = paramsFromSnapshot (all[0].values);
    const CompParams fresh;
    bool same = std::abs (first.speed - fresh.speed) < 1.0e-6f && std::abs (first.upward - fresh.upward) < 1.0e-6f;
    for (size_t b = 0; b < (size_t) numBands; ++b)
        same = same && std::abs (first.comp[b] - fresh.comp[b]) < 1.0e-6f;
    expect (same, "the first preset is the fresh-instance sound");
}

void testCpu()
{
    const double sr = 96000.0;
    Buf in (2, (int) (10.0 * sr));
    const auto loop = referenceLoop (sr);
    for (int pos = 0; pos < in.getNumSamples(); pos += loop.getNumSamples())
        for (int c = 0; c < 2; ++c)
            in.copyFrom (c, pos, loop, c, 0, std::min (loop.getNumSamples(), in.getNumSamples() - pos));
    CompParams p;
    p.analog = true;
    p.upward = 0.5f;
    p.link = 0.5f;
    const auto t0 = std::chrono::steady_clock::now();
    const auto out = process (p, in, sr, 256);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::cout << "  CPU: " << juce::String (100.0 * secs / 10.0, 2) << " % of real time (96 kHz stereo, ANALOGUE, UPWARD)\n";
    expect (secs / 10.0 < 0.05 && finite (out), "CPU below 5 % of real time at 96 kHz");
}

void calibrate()
{
    const double sr = 48000.0;
    const auto in = referenceLoop (sr);
    for (const auto& pr : factoryPresets())
    {
        auto p = paramsFromSnapshot (pr.values);
        p.outputDb = 0.0f;
        const auto out = process (p, in, sr);
        const double level = db (rms (out) / rms (in));
        std::cout << pr.name << "|" << juce::String (-level, 1) << "|peak " << juce::String (db (peak (out)) - level, 2) << "\n";
    }
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "--calibrate")
    {
        calibrate();
        return 0;
    }
    testNeutralIsFlat();
    testNoLatency();
    testCompressionNarrowsRange();
    testBandsIndependent();
    testMixZeroIsDry();
    testSoloBypassGainOutput();
    testUpward();
    testAutoGain();
    testSpeed();
    testStereo();
    testAnalogue();
    testAutomationIsSmooth();
    testBlockSizes();
    testSampleRates();
    testExtremes();
    testPresets();
    testCpu();
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " checks\n";
    return failures == 0 ? 0 : 1;
}
