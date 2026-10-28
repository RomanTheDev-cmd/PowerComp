// PowerComp wrapper tests: processor, state, presets and the editor through the plug-in's shared code.
//   powercomp_wrapper_tests                     all checks
//   powercomp_wrapper_tests --snapshot <dir>    main.png, advanced.png, browser.png, main_small.png, main_large.png
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <iostream>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
int checks = 0, failures = 0;
void expect (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "FAIL: " << what << "\n"; }
}

void setPlain (PowerCompProcessor& p, const char* id, float v)
{
    auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p.state.getParameter (id));
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}
float plain (PowerCompProcessor& p, const char* id) { return p.state.getRawParameterValue (id)->load(); }

// A 100 Hz tone with a 4 kHz tone through processBlock, `blocks` blocks of 512.
juce::AudioBuffer<float> run (PowerCompProcessor& p, int channels, int blocks, double sr = 48000.0)
{
    juce::AudioBuffer<float> all (channels, 512 * blocks), b (channels, 512);
    juce::MidiBuffer m;
    for (int k = 0; k < blocks; ++k)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = (k * 512 + i) / sr;
            const float x = (float) (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * t)
                                     + 0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 4000.0 * t));
            for (int c = 0; c < channels; ++c)
                b.setSample (c, i, x);
        }
        p.processBlock (b, m);
        for (int c = 0; c < channels; ++c)
            all.copyFrom (c, k * 512, b, c, 0, 512);
    }
    return all;
}

void testStateRoundTrip()
{
    PowerCompProcessor a;
    a.prepareToPlay (48000.0, 512);
    setPlain (a, pc::id::lowSplit, 222.0f);
    setPlain (a, pc::id::comp[1], 0.77f);
    setPlain (a, pc::id::analog, 1.0f);
    setPlain (a, pc::id::solo[2], 1.0f);
    juce::MemoryBlock m;
    a.getStateInformation (m);
    PowerCompProcessor b;
    b.prepareToPlay (48000.0, 512);
    b.setStateInformation (m.getData(), (int) m.getSize());
    expect (std::abs (plain (b, pc::id::lowSplit) - 222.0f) < 1.0f && std::abs (plain (b, pc::id::comp[1]) - 0.77f) < 0.001f
                && plain (b, pc::id::analog) > 0.5f && plain (b, pc::id::solo[2]) > 0.5f,
            "state round trip keeps the settings");
}

void testPresets()
{
    PowerCompProcessor p;
    p.prepareToPlay (48000.0, 512);
    const auto& all = p.library.all();
    expect (p.getPresetName() == all[0].name, "a fresh instance shows the first preset (" + p.getPresetName() + ")");
    int ok = 0;
    for (int i = 0; i < (int) all.size(); ++i)
    {
        p.loadPreset (i);
        bool same = p.getPresetName() == all[(size_t) i].name;
        for (const auto& [k, v] : all[(size_t) i].values)
            if (k != pc::id::analog && k != pc::id::autoGain)
                same = same && std::abs (plain (p, k.toRawUTF8()) - v) < 0.01f * std::max (1.0f, std::abs (v));
        ok += same ? 1 : 0;
    }
    expect (ok == (int) all.size(), "every factory preset loads its values (" + juce::String (ok) + " of " + juce::String ((int) all.size()) + ")");
    // Instance settings survive a preset change; a preset clears SOLO.
    setPlain (p, pc::id::analog, 1.0f);
    setPlain (p, pc::id::autoGain, 0.0f);
    setPlain (p, pc::id::solo[0], 1.0f);
    p.loadPreset (2);
    expect (plain (p, pc::id::analog) > 0.5f && plain (p, pc::id::autoGain) < 0.5f, "a preset change keeps DIGITAL | ANALOGUE and AUTO GAIN");
    expect (plain (p, pc::id::solo[0]) < 0.5f, "a preset change clears SOLO");
}

void testProcessing()
{
    PowerCompProcessor p;
    p.prepareToPlay (48000.0, 512);
    expect (p.getLatencySamples() == 0, "no latency reported");
    const auto out = run (p, 2, 100);
    float worst = 0.0f;
    bool finite = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < out.getNumSamples(); ++i)
            finite = finite && std::isfinite (out.getSample (c, i));
    worst = out.getMagnitude (0, 512 * 50, 512 * 50);
    expect (finite && worst > 0.1f && worst < 1.0f, "stereo audio comes through, finite, below 0 dBFS (" + juce::String (worst) + ")");
    expect (p.reductionDb[0].load() > 0.5f, "the LOW meter shows compression of a loud bass (" + juce::String (p.reductionDb[0].load()) + " dB)");

    PowerCompProcessor mono;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.outputBuses.add (juce::AudioChannelSet::mono());
    expect (mono.setBusesLayout (layout), "a mono bus is accepted");
    mono.prepareToPlay (48000.0, 512);
    const auto mout = run (mono, 1, 100);
    float diff = 0.0f;
    for (int i = 0; i < mout.getNumSamples(); ++i)
        diff = std::max (diff, std::abs (mout.getSample (0, i) - out.getSample (0, i)));
    expect (diff < 1.0e-5f, "the mono bus sounds like stereo with the same signal on both sides (" + juce::String (diff) + ")");

    juce::AudioProcessor::BusesLayout odd;
    odd.inputBuses.add (juce::AudioChannelSet::mono());
    odd.outputBuses.add (juce::AudioChannelSet::stereo());
    PowerCompProcessor q;
    expect (! q.isBusesLayoutSupported (odd), "mono in / stereo out is refused (in and out match)");

    // A second prepare at another rate behaves like a fresh instance.
    PowerCompProcessor r1, r2;
    r1.prepareToPlay (44100.0, 512);
    run (r1, 2, 40, 44100.0);
    r1.prepareToPlay (96000.0, 256);
    r1.reset();
    r2.prepareToPlay (96000.0, 256);
    const auto a = run (r1, 2, 40, 96000.0), b = run (r2, 2, 40, 96000.0);
    float d2 = 0.0f;
    for (int i = 0; i < a.getNumSamples(); ++i)
        d2 = std::max (d2, std::abs (a.getSample (0, i) - b.getSample (0, i)));
    expect (d2 < 1.0e-5f, "prepare again at 96 kHz = a fresh instance (" + juce::String (d2) + ")");
}

void testEditor()
{
    PowerCompProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    for (auto [w, h] : { std::pair { 1080, 720 }, std::pair { 864, 576 }, std::pair { 1620, 1080 } })
    {
        ed->setSize (w, h);
        int outside = 0, overlaps = 0;
        juce::Array<juce::Component*> shown;
        for (auto* c : ed->getChildren())
            if (c->isVisible() && c->getComponentID() != "presetBrowser")
            {
                outside += ed->getLocalBounds().contains (c->getBounds()) ? 0 : 1;
                for (auto* o : shown)
                    if (o->getBounds().intersects (c->getBounds()))
                    {
                        ++overlaps;
                        std::cout << "  overlap " << w << "x" << h << ": " << o->getComponentID() << " / " << c->getComponentID() << " ("
                                  << o->getBounds().toString() << " / " << c->getBounds().toString() << ")\n";
                    }
                shown.add (c);
            }
        expect (outside == 0 && overlaps == 0, "editor " + juce::String (w) + "x" + juce::String (h) + ": all controls inside, none overlapping ("
                + juce::String (outside) + " outside, " + juce::String (overlaps) + " overlaps)");
    }
    auto* seg = ed->findChildWithID ("analog");
    expect (seg != nullptr && seg->getNumChildComponents() == 2, "the header has DIGITAL | ANALOGUE");
    auto* disp = dynamic_cast<pc::ui::BandDisplay*> (ed->findChildWithID ("display"));
    expect (disp != nullptr && disp->getHeight() > ed->getHeight() / 6, "a large display");
    if (disp != nullptr)
    {
        // Dragging the LOW|MID line moves the crossover.
        ed->setSize (1080, 720);
        const float x = disp->xForFrequency (plain (p, pc::id::lowSplit));
        const float y = disp->plotArea().getCentreY();
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto t = juce::Time::getCurrentTime();
        auto ev = [&] (float px, bool down)
        {
            return juce::MouseEvent (src, { px, y }, down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     disp, disp, t, { x, y }, t, 1, false);
        };
        const float target = disp->xForFrequency (400.0f);
        disp->mouseDown (ev (x, true));
        disp->mouseDrag (ev (target, true));
        disp->mouseUp (ev (target, false));
        expect (std::abs (plain (p, pc::id::lowSplit) - 400.0f) < 10.0f, "dragging the LOW|MID line sets it (" + juce::String (plain (p, pc::id::lowSplit)) + " Hz)");
        disp->mouseDoubleClick (ev (target, false));
        expect (std::abs (plain (p, pc::id::lowSplit) - 150.0f) < 1.0f, "double click resets it (" + juce::String (plain (p, pc::id::lowSplit)) + " Hz)");
    }
}

void snapshot (const juce::File& dir)
{
    dir.createDirectory();
    PowerCompProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    // Some music through the processor so the meters and the spectrum show something.
    auto feed = [&p]
    {
        juce::Random rng (5);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        for (int k = 0; k < 40; ++k)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double t = (k * 512 + i) / 48000.0;
                const float x = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * t)
                                         + 0.12 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t)
                                         + 0.05 * (rng.nextFloat() * 2.0f - 1.0f));
                b.setSample (0, i, x);
                b.setSample (1, i, x);
            }
            p.processBlock (b, m);
        }
    };
    auto save = [&dir] (juce::Component& c, const juce::String& name)
    {
        const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (dir.getChildFile (name));
        out.setPosition (0); out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
    };
    ed->setSize (1080, 720);
    if (auto* disp = dynamic_cast<pc::ui::BandDisplay*> (ed->findChildWithID ("display")))
        for (int i = 0; i < 8; ++i) { feed(); disp->updateSpectrum(); }
    for (int b = 0; b < pc::numBands; ++b) // the meters as the editor's timer would set them
        if (auto* gm = dynamic_cast<pc::ui::GainMeter*> (ed->findChildWithID ("meter" + juce::String (b))))
            gm->setValues (p.reductionDb[(size_t) b].load(), p.boostDb[(size_t) b].load());
    save (*ed, "main.png");
    if (auto* adv = ed->findChildWithID ("advancedButton"))
        if (auto* b = dynamic_cast<juce::Button*> (adv)) { b->setToggleState (true, juce::sendNotification); save (*ed, "advanced.png"); b->setToggleState (false, juce::sendNotification); }
    if (auto* pb = ed->findChildWithID ("presetButton"))
        if (auto* b = dynamic_cast<juce::Button*> (pb)) { b->onClick(); save (*ed, "browser.png"); b->onClick(); }
    ed->setSize (864, 576); save (*ed, "main_small.png");
    ed->setSize (1620, 1080); save (*ed, "main_large.png");
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
    {
        snapshot (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2])));
        return 0;
    }
    testStateRoundTrip();
    testPresets();
    testProcessing();
#if JUCE_MAC || JUCE_WINDOWS
    testEditor();
#endif
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " wrapper checks\n";
    return failures == 0 ? 0 : 1;
}
