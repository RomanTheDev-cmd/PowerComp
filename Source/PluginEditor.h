#pragma once

#include "PluginProcessor.h"

namespace pc::ui
{
namespace colours
{
    // Graphite to warm white like PowerKick and PowerBass, with a cold blue accent of its own
    // (PowerKick white, PowerBass orange, PowerSynth green).
    inline const juce::Colour bgTop    { 0xff2a2d33 };
    inline const juce::Colour bgBottom { 0xff111216 };
    inline const juce::Colour panelTop { 0xff2f3238 };
    inline const juce::Colour panelBot { 0xff1b1d21 };
    inline const juce::Colour line     { 0x14ffffff };
    inline const juce::Colour text     { 0xffeceae6 };
    inline const juce::Colour dim      { 0xff9a9ca3 };
    inline const juce::Colour faint    { 0xff5b5e66 };
    inline const juce::Colour accent   { 0xff3ec6ff };
    inline const juce::Colour ink      { 0xff111a20 };
    inline const juce::Colour hot      { 0xffff4d4d };
}

// UTF-8 text (the hints are in Russian).
inline juce::String ru (const char* utf8) { return juce::String::fromUTF8 (utf8); }

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getPopupMenuFont() override;
};

// A knob with its name above and its value below. The primary knob of a block is larger and
// carries the accent; the others are smaller with a neutral arc. Text scales with the window.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& title, const juce::String& tooltip);
    void setPrimary (bool);
    bool isPrimary() const { return (bool) slider.getProperties()["primary"]; }
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;

private:
    float scale() const;
    int labelHeight() const;
    juce::String title;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Row of buttons bound to a choice parameter; one label = an on/off button.
class Segment : public juce::Component, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    Segment (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::StringArray& labels,
             const juce::String& tooltip);
    ~Segment() override;
    void resized() override;

private:
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    juce::AudioProcessorValueTreeState& state;
    juce::String paramId;
    juce::OwnedArray<juce::TextButton> buttons;
};

class Meter : public juce::Component
{
public:
    void setLevels (float l, float r);
    void paint (juce::Graphics&) override;

private:
    float levels[2] {}, held[2] {};
    int holdFrames[2] {};
};

// How much a band is compressed right now (accent bar to the left, "-4.2 dB") or lifted by UPWARD (white, "+3.0 dB").
class GainMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    GainMeter();
    void setValues (float reductionDb, float boostDb);
    void paint (juce::Graphics&) override;

private:
    float reduction = 0.0f, boost = 0.0f;
};

// The display: the spectrum of the input (grey) and the output (accent), the three bands, and the two crossover
// lines, which can be dragged (double click: default).
class BandDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit BandDisplay (PowerCompProcessor&);
    void updateSpectrum();
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // Frequency <-> x inside the plot (20 Hz .. 20 kHz, logarithmic).
    float xForFrequency (float hz) const;
    float frequencyForX (float x) const;
    juce::Rectangle<float> plotArea() const;

private:
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder, columns = 240;
    int splitAt (float x) const; // 0 = LOW|MID, 1 = MID|HIGH, -1 = none
    juce::RangedAudioParameter* splitParam (int which) const;
    void computeColumns (const std::array<float, PowerCompProcessor::ringSize>& ring, int write, std::array<float, columns>& out);

    PowerCompProcessor& proc;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> scratch = std::vector<float> ((size_t) fftSize * 2);
    std::array<float, columns> inDb {}, outDb {};
    int lastWrite = -1, hover = -1, dragging = -1;
};

// Hidden settings: AUTO GAIN and STEREO LINK.
class AdvancedPanel : public juce::Component
{
public:
    explicit AdvancedPanel (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Segment autoGain;
    Knob link;
};

// LEARN (2026-10-28): the profile, AMOUNT, LISTEN / STOP, BEFORE | LEARNED and what LEARN is doing. Opens over the
// controls from the LEARN button in the header, like ADVANCED.
class LearnPanel : public juce::Component
{
public:
    explicit LearnPanel (PowerCompProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh(); // from the editor timer
    std::function<void()> onSettingChanged; // BEFORE | LEARNED switched: the header shows the other name

private:
    PowerCompProcessor& proc;
    juce::OwnedArray<juce::TextButton> profiles;
    juce::Slider amount;
    juce::TextButton listenButton { "LISTEN" }, beforeButton { "BEFORE" }, learnedButton { "LEARNED" };
    juce::String status;
    float progress = -1.0f;
};

// Preset browser: opens over the controls from the preset name. Categories on the left, a search field
// and intensity filters on top, every matching preset in one scrolling list. Choosing a preset loads it
// at once; arrow keys step through the list, Enter or Escape closes.
// A five-pointed star in box: filled for a favourite, an outline otherwise.
void drawStar (juce::Graphics&, juce::Rectangle<float> box, bool filled, juce::Colour, float stroke);

// The header star (2026-10-29): stars the current preset, which then shows under Favourites in the browser.
class StarButton : public juce::TextButton
{
public:
    void setStarred (bool on) { if (on != starred) { starred = on; repaint(); } }
    bool isStarred() const { return starred; }
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    bool starred = false; // not the toggle state: a toggled button gets the accent fill, which would hide the star
};

class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (PowerCompProcessor&);
    ~PresetBrowser() override;
    void open();                                // rescan, show the current preset, focus the search
    void setCategory (const juce::String&);     // "" = all presets, "User" = the user's own
    void toggleGroup (const juce::String&);     // intensity; none selected = all
    void setSearch (const juce::String&);
    void choose (int row, bool close);
    void toggleFavourite (int row);             // the star of a row: never loads the preset
    void favouritesChanged();                   // a star changed elsewhere (the header)
    int numShown() const { return (int) shown.size(); }
    int firstVisibleRow() const { return list.getRowContainingPosition (1, 1); }

    bool keyPressed (const juce::KeyPress&) override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onLoaded, onClose, onFavouritesChanged;

private:
    // Click target over the star at the left of a row; the rest of the row passes clicks to the list.
    struct StarSpot : juce::Component, juce::SettableTooltipClient
    {
        explicit StarSpot (PresetBrowser&);
        bool hitTest (int x, int) override { return row >= 0 && row < owner.numShown() && x < owner.starWidth(); }
        void mouseDown (const juce::MouseEvent& e) override { if (e.getNumberOfClicks() == 1) owner.toggleFavourite (row); }
        PresetBrowser& owner;
        int row = -1;
    };
    struct SearchField : juce::TextEditor
    {
        std::function<bool (const juce::KeyPress&)> onKey;
        bool keyPressed (const juce::KeyPress& k) override { return (onKey && onKey (k)) || juce::TextEditor::keyPressed (k); }
    };
    struct Categories : juce::Component
    {
        explicit Categories (PresetBrowser& b) : owner (b) {}
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override;
        int rowAt (int y) const;
        PresetBrowser& owner;
        int hover = -1;
    };

    int getNumRows() override { return numShown(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    void selectedRowsChanged (int lastRow) override;
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { close(); }
    void returnKeyPressed (int) override { close(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void refilter();
    void rebuildGroups();
    void selectCurrent();
    void close() { if (onClose) onClose(); }
    float scale() const { return (float) getWidth() / 1032.0f; }
    int starWidth() const { return juce::roundToInt (30.0f * scale()); }

    PowerCompProcessor& proc;
    SearchField search;
    juce::TextButton closeButton { "CLOSE" }, allTypesButton { "ALL" };
    juce::OwnedArray<juce::TextButton> groupButtons;
    Categories categories { *this };
    juce::ListBox list { "Presets", this };
    juce::StringArray categoryNames, groupNames, selectedGroups;
    juce::String category;
    std::vector<int> shown;
    bool updating = false;
    int hoverRow = -1;
    bool hoverStar = false;
    juce::Rectangle<int> titleArea, searchArea, emptyArea;
};
}

class PowerCompEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PowerCompEditor (PowerCompProcessor&);
    ~PowerCompEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showMainMenu();
    void stepPreset (int delta);
    void savePreset();
    void askForName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done);
    void showError (const juce::String& message);
    void refreshHeader(); // the preset name and its star
    juce::String bandRange (int band) const; // "below 150 Hz", "150 Hz - 2.5 kHz", "above 2.5 kHz"

    PowerCompProcessor& proc;
    pc::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "SAVE" }, advancedButton { "ADVANCED" },
        menuButton { "..." }, learnButton { "LEARN" };
    pc::ui::StarButton favouriteButton; // the current preset's star (2026-10-29)

    std::array<std::unique_ptr<pc::ui::Knob>, pc::numBands> compress, gain;
    std::array<std::unique_ptr<pc::ui::Segment>, pc::numBands> solo, bypass;
    std::array<std::unique_ptr<pc::ui::GainMeter>, pc::numBands> gainMeters;
    pc::ui::Knob lowSplit, highSplit, speed, upward, mix, output;
    pc::ui::Segment analogMode;
    pc::ui::BandDisplay display;
    pc::ui::Meter meter;
    pc::ui::AdvancedPanel advanced;
    pc::ui::LearnPanel learnPanel;
    void toggleLearn();
    pc::ui::PresetBrowser browser;

    std::unique_ptr<juce::FileChooser> chooser;
    struct Section { juce::String title; juce::Rectangle<int> area; int band = -1; };
    std::vector<Section> sections;
    float shownSplits[2] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerCompEditor)
};
