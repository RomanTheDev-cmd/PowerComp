#include "Params.h"

namespace pc
{
namespace
{
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using Attr = juce::AudioParameterFloatAttributes;
using Range = juce::NormalisableRange<float>;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String dbText (float v, int)
{
    const float r = std::round (v * 10.0f) / 10.0f;
    return (r > 0.0f ? "+" : "") + juce::String (r == 0.0f ? 0.0f : r, 1) + " dB";
}

Range skewed (float lo, float hi, float centre)
{
    Range r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

void flt (Layout& l, const char* pid, const char* name, Range r, float def, std::function<juce::String (float, int)> text)
{
    l.add (std::make_unique<APF> (juce::ParameterID { pid, 1 }, name, r, def, Attr().withStringFromValueFunction (std::move (text))));
}

void choice (Layout& l, const char* pid, const char* name, const juce::StringArray& items, int def)
{
    l.add (std::make_unique<APC> (juce::ParameterID { pid, 1 }, name, items, def));
}

const char* bandName[numBands] = { "Low", "Mid", "High" };
}

juce::String hzText (float hz)
{
    if (hz < 1000.0f)
        return juce::String (juce::roundToInt (hz)) + " Hz";
    return juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz";
}

const juce::StringArray& allParamIds()
{
    static const juce::StringArray ids = []
    {
        juce::StringArray s;
        for (int b = 0; b < numBands; ++b)
            s.addArray ({ id::comp[b], id::gain[b], id::solo[b], id::bypass[b] });
        s.addArray ({ id::lowSplit, id::highSplit, id::speed, id::upward, id::mix, id::output, id::autoGain, id::link, id::analog });
        return s;
    }();
    return ids;
}

// Defaults: the CompParams member initialisers (one place).
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    const CompParams d;
    Layout l;
    for (int b = 0; b < numBands; ++b)
    {
        const juce::String n (bandName[b]);
        flt (l, id::comp[b], (n + " Compress").toRawUTF8(), Range (0.0f, 1.0f), d.comp[(size_t) b], pct);
        flt (l, id::gain[b], (n + " Gain").toRawUTF8(), Range (-12.0f, 12.0f, 0.1f), d.gainDb[(size_t) b], dbText);
        choice (l, id::solo[b], (n + " Solo").toRawUTF8(), { "Off", "On" }, d.solo[(size_t) b] ? 1 : 0);
        choice (l, id::bypass[b], (n + " Bypass").toRawUTF8(), { "Off", "On" }, d.bypass[(size_t) b] ? 1 : 0);
    }
    flt (l, id::lowSplit, "Low/Mid Split", skewed (lowSplitMin, lowSplitMax, 150.0f), d.lowSplitHz, [] (float v, int) { return hzText (v); });
    flt (l, id::highSplit, "Mid/High Split", skewed (highSplitMin, highSplitMax, 3000.0f), d.highSplitHz, [] (float v, int) { return hzText (v); });
    flt (l, id::speed, "Speed", Range (0.0f, 1.0f), d.speed, pct);
    flt (l, id::upward, "Upward", Range (0.0f, 1.0f), d.upward, pct);
    flt (l, id::mix, "Mix", Range (0.0f, 1.0f), d.mix, pct);
    flt (l, id::output, "Output", Range (-24.0f, 12.0f, 0.1f), d.outputDb, dbText);
    choice (l, id::autoGain, "Auto Gain", { "Off", "On" }, (int) d.autoGain);
    flt (l, id::link, "Stereo Link", Range (0.0f, 1.0f), d.link, pct);
    // DIGITAL | ANALOGUE, as in PowerKick, PowerBass and PowerSynth: Analogue adds warm saturation to LOW and MID.
    choice (l, id::analog, "Digital/Analogue", { "Digital", "Analogue" }, (int) d.analog);
    return l;
}

CompParams paramsFrom (const std::function<float (const char*, float)>& get)
{
    CompParams p;
    auto on = [&get] (const char* pid, bool def) { return get (pid, def ? 1.0f : 0.0f) > 0.5f; };
    for (size_t b = 0; b < (size_t) numBands; ++b)
    {
        p.comp[b] = juce::jlimit (0.0f, 1.0f, get (id::comp[b], p.comp[b]));
        p.gainDb[b] = juce::jlimit (-12.0f, 12.0f, get (id::gain[b], p.gainDb[b]));
        p.solo[b] = on (id::solo[b], p.solo[b]);
        p.bypass[b] = on (id::bypass[b], p.bypass[b]);
    }
    p.lowSplitHz = juce::jlimit (lowSplitMin, lowSplitMax, get (id::lowSplit, p.lowSplitHz));
    p.highSplitHz = juce::jlimit (highSplitMin, highSplitMax, get (id::highSplit, p.highSplitHz));
    p.speed = juce::jlimit (0.0f, 1.0f, get (id::speed, p.speed));
    p.upward = juce::jlimit (0.0f, 1.0f, get (id::upward, p.upward));
    p.mix = juce::jlimit (0.0f, 1.0f, get (id::mix, p.mix));
    p.outputDb = juce::jlimit (-24.0f, 12.0f, get (id::output, p.outputDb));
    p.autoGain = on (id::autoGain, p.autoGain);
    p.link = juce::jlimit (0.0f, 1.0f, get (id::link, p.link));
    p.analog = on (id::analog, p.analog);
    return p;
}

CompParams readParams (const juce::AudioProcessorValueTreeState& state)
{
    return paramsFrom ([&state] (const char* pid, float def)
    {
        if (auto* v = state.getRawParameterValue (pid)) return v->load();
        return def;
    });
}
}
