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

// LEARN through the processor (2026-10-28): listening does not change the sound, the result lands on the ordinary
// parameters, BEFORE | LEARNED switch exactly, AMOUNT re-applies from the same capture, the profile and AMOUNT are
// saved with the project, presets still load.
juce::AudioBuffer<float> beat (int blocks, double sr = 48000.0, float gain = 0.7f)
{
    juce::AudioBuffer<float> b (2, 512 * blocks);
    juce::Random rng (5);
    const double pi = juce::MathConstants<double>::pi, period = 60.0 / 140.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / sr, tb = std::fmod (t, period);
        const float x = (float) (gain * (std::exp (-tb / 0.15) * std::sin (2.0 * pi * 50.0 * tb) + 0.3 * std::exp (-std::fmod (t, period / 4) / 0.02) * (rng.nextFloat() - 0.5f)
                                         + 0.1 * std::sin (2.0 * pi * 440.0 * t)));
        b.setSample (0, i, x);
        b.setSample (1, i, x);
    }
    return b;
}

juce::AudioBuffer<float> feed (PowerCompProcessor& p, const juce::AudioBuffer<float>& in)
{
    juce::AudioBuffer<float> out (in), block (2, 512);
    juce::MidiBuffer m;
    for (int pos = 0; pos + 512 <= in.getNumSamples(); pos += 512)
    {
        for (int c = 0; c < 2; ++c) block.copyFrom (c, 0, in, c, pos, 512);
        p.processBlock (block, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, block, c, 0, 512);
    }
    return out;
}

void testLearn()
{
    const auto in = beat (94 * 6); // about 6 s
    PowerCompProcessor ref, p;
    ref.prepareToPlay (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    const auto before = pc::captureSnapshot (p.state);
    const auto nameBefore = p.getPresetName();
    p.startLearning();
    expect (p.getLearnState() == PowerCompProcessor::LearnState::listening, "LEARN starts listening");
    const auto a = feed (ref, in), b = feed (p, in);
    bool same = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < a.getNumSamples(); ++i) same = same && a.getSample (c, i) == b.getSample (c, i);
    expect (same, "listening does not change the sound");
    expect (p.learnedSeconds() > 5.5 && p.learnedSeconds() < 6.5, "LEARN captured the played audio (" + juce::String (p.learnedSeconds(), 2) + " s)");
    p.stopLearning();
    p.waitForLearn();
    expect (p.getLearnState() == PowerCompProcessor::LearnState::idle && p.hasLearned() && p.isShowingLearned(), "LEARN analyses and applies");
    const auto learned = pc::captureSnapshot (p.state);
    const auto s = p.lastLearn(); // a copy: lastLearn() changes when AMOUNT re-applies
    bool applied = s.valid;
    for (int b2 = 0; b2 < pc::numBands; ++b2) applied = applied && std::abs (plain (p, pc::id::comp[b2]) - s.params.comp[(size_t) b2]) < 0.01f;
    expect (applied && learned != before, "the suggestion lands on the ordinary parameters");
    expect (p.getPresetName().startsWith ("LEARN"), "the header shows the learned setting (" + p.getPresetName() + ")");
    // Manual changes after LEARN work like on any setting.
    setPlain (p, pc::id::comp[1], 0.9f);
    expect (std::abs (plain (p, pc::id::comp[1]) - 0.9f) < 0.01f, "the knobs stay manual after LEARN");
    // BEFORE | LEARNED switch exactly (each keeps its own changes).
    p.showLearned (false);
    expect (pc::captureSnapshot (p.state) == before && p.getPresetName() == nameBefore && ! p.isShowingLearned(), "BEFORE restores the setting before LEARN exactly");
    p.showLearned (true);
    expect (std::abs (plain (p, pc::id::comp[1]) - 0.9f) < 0.01f && p.isShowingLearned(), "LEARNED comes back with the manual change");
    // AMOUNT re-applies from the same capture, without listening again.
    p.setLearnProfile (pc::learn::Profile::drums);
    p.setLearnAmount (1.0f);
    p.waitForLearn();
    expect (p.lastLearn().valid && p.lastLearn().profile == pc::learn::Profile::drums && p.lastLearn().targetDb[1] > s.targetDb[1],
            "AMOUNT and the profile re-apply from the same capture");
    // Saved with the project; presets do not change them; an old project opens with AUTO and 50 %.
    juce::MemoryBlock m;
    p.getStateInformation (m);
    PowerCompProcessor q;
    q.setStateInformation (m.getData(), (int) m.getSize());
    expect (q.getLearnProfile() == pc::learn::Profile::drums && std::abs (q.getLearnAmount() - 1.0f) < 1.0e-6f, "the project keeps the LEARN profile and AMOUNT");
    q.loadPreset (3);
    expect (q.getLearnProfile() == pc::learn::Profile::drums && ! q.hasLearned(), "a preset loads after LEARN and keeps the LEARN choice");
    PowerCompProcessor fresh;
    expect (fresh.getLearnProfile() == pc::learn::Profile::automatic && std::abs (fresh.getLearnAmount() - 0.5f) < 1.0e-6f, "a project without LEARN opens with AUTO, 50 %");
    // Too little sound: nothing changes.
    PowerCompProcessor r;
    r.prepareToPlay (48000.0, 512);
    const auto keep = pc::captureSnapshot (r.state);
    r.startLearning();
    feed (r, beat (94));
    r.stopLearning();
    r.waitForLearn();
    expect (! r.hasLearned() && pc::captureSnapshot (r.state) == keep && r.getLearnState() == PowerCompProcessor::LearnState::idle,
            "LEARN with less than 2 s of sound changes nothing");
    // A full capture (20 s) stops listening by itself.
    PowerCompProcessor f;
    f.prepareToPlay (44100.0, 512);
    f.startLearning();
    feed (f, beat (94 * 22, 44100.0));
    f.waitForLearn();
    expect (f.hasLearned() && f.learnedSeconds() <= 20.01, "LEARN stops by itself after 20 s of sound (" + juce::String (f.learnedSeconds(), 2) + " s)");
}

// User presets are .pcpreset (1.0.0 saved them as .pspreset by mistake: those still load).
void testPresetFiles()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PowerCompPresetTest-" + juce::String (juce::Random::getSystemRandom().nextInt()));
    dir.createDirectory();
    pc::PresetLibrary lib (dir);
    juce::String err;
    lib.saveUser ("Mine", pc::factoryPresets()[1].values, err);
    expect (dir.getChildFile ("Mine.pcpreset").existsAsFile(), "a user preset is saved as .pcpreset");
    dir.getChildFile ("Mine.pcpreset").copyFileTo (dir.getChildFile ("Old.pspreset"));
    lib.rescan();
    expect (lib.indexOf ("Mine") >= 0 && lib.indexOf ("Old") >= 0, "user presets of 1.0.0 (.pspreset) still load");
    dir.moveToTrash();
}

void testEditor()
{
    PowerCompProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    if (auto* lb = dynamic_cast<juce::Button*> (ed->findChildWithID ("learnButton")))
    {
        auto* panel = ed->findChildWithID ("learnPanel");
        lb->onClick();
        const bool started = panel != nullptr && panel->isVisible() && p.getLearnState() == PowerCompProcessor::LearnState::listening;
        lb->onClick();
        const bool stopped = p.getLearnState() == PowerCompProcessor::LearnState::idle; // nothing heard: cancelled
        lb->onClick();
        expect (started && stopped && ! panel->isVisible(), "one click on LEARN opens its panel and listens, the next stops, the next closes");
    }
    else
        expect (false, "the header has a LEARN button");
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
    // LEARN: listening, then the result.
    if (auto* lb = dynamic_cast<juce::Button*> (ed->findChildWithID ("learnButton")))
    {
        lb->onClick();
        ::feed (p, beat (94 * 3));
        if (auto* panel = dynamic_cast<pc::ui::LearnPanel*> (ed->findChildWithID ("learnPanel"))) panel->refresh();
        save (*ed, "learn_listening.png");
        ::feed (p, beat (94 * 5));
        lb->onClick();
        p.waitForLearn();
        lb->onClick(); // closes the panel and updates the header
        lb->onClick(); // opens it again with the result (no new listening after a LEARN)
        save (*ed, "learn_done.png");
        lb->onClick();
    }
    ed->setSize (864, 576); save (*ed, "main_small.png");
    ed->setSize (1620, 1080); save (*ed, "main_large.png");
}
}

// Favourites (2026-10-29). A temporary preset folder, never the user's own; left in the system temp folder.
juce::File tempPresetDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("PowerCompFavouritesTest-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
    dir.createDirectory();
    return dir;
}
juce::StringArray starredNames (const pc::PresetLibrary& l)
{
    juce::StringArray out;
    for (int i : pc::filterPresets (l.all(), pc::favouritesCategory, {}, {}))
        out.add (l.all()[(size_t) i].name + (l.all()[(size_t) i].isFactory() ? "" : " (user)"));
    return out;
}
int userIndex (const pc::PresetLibrary& l, const juce::String& name)
{
    for (int i = 0; i < (int) l.all().size(); ++i)
        if (! l.all()[(size_t) i].isFactory() && l.all()[(size_t) i].name == name)
            return i;
    return -1;
}

void testFavourites()
{
    const auto dir = tempPresetDir();
    auto trashed = dir.getChildFile ("trashed");
    trashed.createDirectory();
    auto fakeTrash = [trashed] (const juce::File& f) { return f.moveFileTo (trashed.getChildFile (f.getFileName())); };
    juce::String e;
    pc::PresetLibrary a (dir);
    a.moveToTrash = fakeTrash;
    expect (starredNames (a).isEmpty(), "no favourites in a new preset folder");
    const juce::String shared = a.all()[3].name; // a factory name a user preset will share
    a.saveUser (shared, a.all()[3].values, e);
    a.saveUser ("My Lead", a.all()[0].values, e);
    const bool on = a.toggleFavourite (3);
    expect (on && a.all()[3].favourite && ! a.all()[(size_t) userIndex (a, shared)].favourite,
            "a star on a factory preset leaves the user preset of the same name unstarred");

    pc::PresetLibrary b (dir); // another instance, or another project
    b.moveToTrash = fakeTrash;
    expect (b.all()[3].favourite, "the star shows in another instance");
    b.toggleFavourite (userIndex (b, "My Lead"));
    a.rescan();
    expect (starredNames (a) == juce::StringArray (shared, "My Lead (user)"),
            "Favourites lists the starred presets in library order, both instances' stars kept (" + starredNames (a).joinIntoString (", ") + ")");
    expect (pc::filterPresets (a.all(), pc::favouritesCategory, {}, "my lead").size() == 1, "search works inside Favourites");

    expect (a.rename (userIndex (a, "My Lead"), "Lead X", e) && a.all()[(size_t) userIndex (a, "Lead X")].favourite,
            "a renamed user preset keeps its star");
    b.rescan();
    expect (starredNames (b).contains ("Lead X (user)") && ! starredNames (b).contains ("My Lead (user)"), "the rename reaches other instances");
    expect (a.remove (userIndex (a, "Lead X"), e), "a starred user preset can be deleted: " + e);
    a.saveUser ("Lead X", a.all()[0].values, e); // a new preset of that name starts without a star
    expect (! a.all()[(size_t) userIndex (a, "Lead X")].favourite && starredNames (a) == juce::StringArray (shared),
            "a deleted preset loses its star (" + starredNames (a).joinIntoString (", ") + ")");
    expect (! a.toggleFavourite (3) && ! a.all()[3].favourite && starredNames (a).isEmpty(), "a second click removes the star");

    a.toggleFavourite (5);
    a.favouritesFile().replaceWithText ("{ not json at all");
    pc::PresetLibrary c (dir);
    expect (starredNames (c).isEmpty() && c.all().size() == a.all().size(), "a damaged favourites file loads as no stars, every preset still there");
    c.toggleFavourite (7);
    pc::PresetLibrary d (dir);
    expect (d.all()[7].favourite && starredNames (d).size() == 1, "after a damaged file the next star writes a good one");
}

// The browser: the star at the left of a row stars that preset without loading it; the rest of the row loads it.
// Favourites is a category of the browser; the header star stars the current preset.
void testFavouritesInEditor()
{
    PowerCompProcessor p;
    p.prepareToPlay (48000.0, 512);
    p.library = pc::PresetLibrary (tempPresetDir());
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (1080, 720);
    auto* star = dynamic_cast<pc::ui::StarButton*> (ed->findChildWithID ("favouriteButton"));
    expect (star != nullptr, "the header has a star for the current preset");
    if (star == nullptr)
        return;
    const auto current = p.getPresetName();
    star->onClick();
    const int ci = p.library.indexOf (current);
    expect (ci >= 0 && p.library.all()[(size_t) ci].favourite && star->isStarred(), "the header star stars the current preset");

    auto* presetButton = dynamic_cast<juce::Button*> (ed->findChildWithID ("presetButton"));
    presetButton->onClick();
    auto* browser = dynamic_cast<pc::ui::PresetBrowser*> (ed->findChildWithID ("presetBrowser"));
    auto* list = browser != nullptr ? dynamic_cast<juce::ListBox*> (browser->findChildWithID ("presetList")) : nullptr;
    expect (list != nullptr && browser->isVisible(), "the browser opens with its list");
    if (list == nullptr)
        return;
    browser->setCategory ({});
    // A row on screen (the list scrolls to the current preset) that is not the current preset.
    const auto order = pc::filterPresets (p.library.all(), {}, {}, {});
    int row = browser->firstVisibleRow();
    while (row < browser->numShown() - 1 && p.library.all()[(size_t) order[(size_t) row]].name == current)
        ++row;
    auto* spot = list->getComponentForRowNumber (row); // the row's own component: the star's click target
    auto* rowComp = spot != nullptr ? spot->getParentComponent() : nullptr;
    expect (rowComp != nullptr, "row " + juce::String (row) + " is on screen with its star (list " + list->getBounds().toString() + ", "
            + juce::String (list->getNumRowsOnScreen()) + " rows on screen)");
    if (rowComp == nullptr)
        return;
    const int h = rowComp->getHeight();
    auto* atStar = rowComp->getComponentAt (juce::roundToInt (12 * ed->getWidth() / 1080.0f), h / 2);
    auto* atName = rowComp->getComponentAt (rowComp->getWidth() / 2, h / 2);
    expect (atStar != nullptr && atStar != rowComp && atName == rowComp, "the star is a click target of its own; the name belongs to the row");
    const int preset = order[(size_t) row];
    if (atStar != nullptr && atStar != rowComp)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const juce::Point<float> at (12.0f, (float) h / 2);
        const juce::MouseEvent click (src, at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, atStar, atStar, juce::Time::getCurrentTime(), at,
                                      juce::Time::getCurrentTime(), 1, false);
        atStar->mouseDown (click);
    }
    expect (p.library.all()[(size_t) preset].favourite, "clicking the star of a row stars that preset");
    expect (p.getPresetName() == current, "starring a row does not load it (still " + p.getPresetName() + ")");
    browser->setCategory (pc::favouritesCategory);
    expect (browser->numShown() == 2, "the Favourites category shows the two starred presets (" + juce::String (browser->numShown()) + ")");
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
    {
        snapshot (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2])));
        return 0;
    }
    testFavourites();
    testStateRoundTrip();
    testPresets();
    testProcessing();
    testLearn();
    testPresetFiles();
#if JUCE_MAC || JUCE_WINDOWS
    testEditor();
    testFavouritesInEditor();
#endif
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " wrapper checks\n";
    return failures == 0 ? 0 : 1;
}
