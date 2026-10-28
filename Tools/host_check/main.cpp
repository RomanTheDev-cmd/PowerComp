// host_check <path to .vst3>: loads an EFFECT plug-in through the VST3 interface, as a DAW does, and checks the
// shipped binary itself: one effect in the bundle, an instance, audio in -> audio out at several sample rates and
// block sizes (present, finite, changed by the default compression, below 0 dBFS), a state round trip, the editor
// in a window (opens, processes along, closes). Exit code 0 only if every check passed.
// Adapted from PowerBass/Tools/host_check (instruments: MIDI in) for PowerComp (2026-10-28).
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("%s %s\n", ok ? "OK  " : "FAIL", what.toRawUTF8());
    std::fflush (stdout);
    if (! ok) ++failures;
}

void stage (const char* what) { std::printf ("     ... %s\n", what); std::fflush (stdout); }

// A kick-like 55 Hz hit every beat at 130 BPM with 16th-note noise hats, peak about -3 dBFS.
float source (double t)
{
    const double beat = 60.0 / 130.0, tb = std::fmod (t, beat), t16 = std::fmod (t, beat / 4.0);
    const double kick = 0.6 * std::exp (-tb / 0.2) * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * tb);
    const double hat = 0.1 * std::exp (-t16 / 0.02) * std::sin (2.0 * juce::MathConstants<double>::pi * 7919.0 * t * (1.0 + 0.3 * std::sin (t * 977.0)));
    return (float) (kick + hat);
}

// The source through the plug-in in host blocks; also returns the input.
juce::AudioBuffer<float> run (juce::AudioPluginInstance& p, double sr, int block, double seconds, juce::AudioBuffer<float>* input = nullptr)
{
    const int total = (int) (seconds * sr), channels = std::max (2, p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> out (2, total), buf (channels, block);
    if (input != nullptr) input->setSize (2, total);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (channels, n, false, false, true);
        buf.clear();
        for (int i = 0; i < n; ++i)
        {
            const float x = source ((pos + i) / sr);
            for (int c = 0; c < std::min (2, channels); ++c)
                buf.setSample (c, i, x);
            if (input != nullptr) { input->setSample (0, pos + i, x); input->setSample (1, pos + i, x); }
        }
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, pos, buf, std::min (c, channels - 1), 0, n);
    }
    return out;
}
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: host_check <path to .vst3>\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> types;
    vst3.findAllTypesForFile (types, juce::String::fromUTF8 (argv[1]));
    check (types.size() == 1, "the VST3 bundle holds one plug-in (" + juce::String (types.size()) + ")");
    if (types.isEmpty()) return 1;
    const auto& d = *types[0];
    std::printf ("     %s by %s, version %s, category %s\n", d.name.toRawUTF8(), d.manufacturerName.toRawUTF8(), d.version.toRawUTF8(),
                 d.category.toRawUTF8());
    check (! d.isInstrument, "it is an effect");

    juce::String error;
    auto p = vst3.createInstanceFromDescription (d, 48000.0, 512, error);
    check (p != nullptr, "an instance is created" + (error.isEmpty() ? juce::String() : " (" + error + ")"));
    if (p == nullptr) return 1;
    std::printf ("     %d parameters, %d inputs, %d outputs, latency %d samples\n", p->getParameters().size(),
                 p->getTotalNumInputChannels(), p->getTotalNumOutputChannels(), p->getLatencySamples());
    check (p->getParameters().size() >= 20, "its parameters are visible to the host");
    check (p->getLatencySamples() == 0, "no latency");

    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 64, 512 })
        {
            p->setPlayConfigDetails (2, 2, sr, block);
            p->prepareToPlay (sr, block);
            juce::AudioBuffer<float> in;
            const auto a = run (*p, sr, block, 2.0, &in);
            bool finite = true;
            double diff = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < a.getNumSamples(); ++i)
                {
                    finite = finite && std::isfinite (a.getSample (c, i));
                    diff = std::max (diff, (double) std::abs (a.getSample (c, i) - in.getSample (c, i)));
                }
            const float peak = a.getMagnitude (0, a.getNumSamples());
            check (finite && peak > 0.05f && peak <= 1.0f && diff > 0.001,
                   "audio through at " + juce::String ((int) sr) + " Hz, block " + juce::String (block) + ": peak "
                       + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS, changed by up to " + juce::String (diff, 3)
                       + (finite ? "" : ", NOT FINITE"));
            p->releaseResources();
        }

    // State round trip through the plug-in's own chunk.
    auto* param = p->getParameters()[0];
    param->setValueNotifyingHost (0.3f);
    juce::MemoryBlock state;
    p->getStateInformation (state);
    param->setValueNotifyingHost (0.8f);
    p->setStateInformation (state.getData(), (int) state.getSize());
    check (state.getSize() > 0 && std::abs (param->getValue() - 0.3f) < 0.02f,
           "state saved and restored (" + juce::String ((int) state.getSize()) + " bytes, " + param->getName (32) + ")");

    // The editor as a DAW shows it: in a window on screen, a few seconds of the message loop (timers, spectrum,
    // painting), then the window closes (JUCE's VBlankThread hang under Wine is fixed by cmake/patch_juce.cmake).
    if (p->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorIfNeeded());
        check (editor != nullptr && editor->getWidth() > 300 && editor->getHeight() > 200,
               "the editor is created (" + (editor != nullptr ? juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight()) : juce::String ("none")) + ")");
        if (editor != nullptr)
        {
            auto window = std::make_unique<juce::DocumentWindow> ("host_check", juce::Colours::black, juce::DocumentWindow::closeButton);
            window->setUsingNativeTitleBar (true);
            window->setContentNonOwned (editor.get(), true);
            window->setVisible (true);
            p->setPlayConfigDetails (2, 2, 48000.0, 512);
            p->prepareToPlay (48000.0, 512);
            const auto a = run (*p, 48000.0, 512, 0.5);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (3000);
            check (window->isOnDesktop() && editor->isShowing() && editor->getWidth() > 300,
                   "the editor shows in a window (" + juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight()) + ")");
            check (a.getMagnitude (0, a.getNumSamples()) > 0.05f, "audio while the editor is open");
            stage ("closing the editor: removing it from the window");
            window->clearContentComponent();
            stage ("closing the editor: closing the window");
            window.reset();
            stage ("closing the editor: deleting the editor");
            editor.reset();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            p->releaseResources();
            check (true, "the editor closes");
        }
    }
    stage ("deleting the instance");
    p.reset();
    stage ("unloading");
    std::printf (failures == 0 ? "PASS\n" : "FAILED %d\n", failures);
    std::fflush (stdout);
    return failures == 0 ? 0 : 1;
}
