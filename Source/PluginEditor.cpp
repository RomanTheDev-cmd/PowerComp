#include "PluginEditor.h"

using namespace pc::ui;

namespace
{
juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

void fillPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
{
    g.setGradientFill (juce::ColourGradient (colours::panelTop, r.getX(), r.getY(), colours::panelBot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.09f), r.getX(), r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.5f, false));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

// Overlay panels (ADVANCED, presets).
void fillOverlay (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0, 4), 12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff30343b), 0, r.getY(), juce::Colour (0xff17191d), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (colours::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
}

float windowScale (const juce::Component& c)
{
    const auto* top = c.getTopLevelComponent();
    return top != nullptr && top->getWidth() > 0 ? (float) top->getWidth() / 1080.0f : 1.0f;
}

const char* bandTitles[pc::numBands] = { "LOW", "MID", "HIGH" };
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, colours::panelBot);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::ink);
    setColour (juce::PopupMenu::headerTextColourId, colours::dim);
    setColour (juce::TooltipWindow::backgroundColourId, colours::panelTop);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::line);
    setColour (juce::AlertWindow::backgroundColourId, colours::panelBot);
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::AlertWindow::outlineColourId, colours::line);
    setColour (juce::TextEditor::backgroundColourId, colours::bgBottom);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::line);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::TextButton::buttonColourId, colours::panelTop);
    setColour (juce::TextButton::textColourOffId, colours::text);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float track = std::max (2.5f, radius * 0.09f);
    const float angle = start + pos * (end - start);

    const float face = radius - track * 2.2f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff474a52), c.x, c.y - face, juce::Colour (0xff1f2126), c.x, c.y + face, false));
    g.fillEllipse (c.x - face, c.y - face, face * 2.0f, face * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (c.x - face, c.y - face, face * 2.0f, face * 2.0f, 1.0f);

    const float r = radius - track * 0.5f;
    juce::Path bg;
    bg.addCentredArc (c.x, c.y, r, r, 0.0f, start, end, true);
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getProperties().contains ("bipolar");
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.0f, std::min (from, angle), std::max (from, angle), true);
        // The accent marks the primary knob of each block; the others draw a quieter neutral arc.
        const bool primary = (bool) s.getProperties()["primary"];
        g.setColour (! s.isEnabled() ? colours::faint : (primary ? colours::accent : colours::text.withAlpha (0.62f)));
        g.strokePath (val, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (colours::text);
    g.drawLine ({ c.getPointOnCircumference (face * 0.38f, angle), c.getPointOnCircumference (face * 0.78f, angle) },
                std::max (2.0f, radius * 0.06f));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = std::min (8.0f, r.getHeight() * 0.5f);
    if (b.getToggleState())
    {
        g.setColour (colours::accent.withAlpha (down ? 0.8f : 1.0f));
        g.fillRoundedRectangle (r, radius);
        return;
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff383b42).brighter (over ? 0.12f : 0.0f), r.getX(), r.getY(),
                                             juce::Colour (0xff23252a), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (over ? 0.16f : 0.08f));
    g.drawRoundedRectangle (r, radius, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? colours::ink : (b.isEnabled() ? colours::text : colours::faint));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.setGradientFill (juce::ColourGradient (colours::panelTop, 0, 0, colours::panelBot, 0, (float) h, false));
    g.fillAll();
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    // Grows with the window like the knob labels (1080 px wide = 1.0).
    return uiFont (std::min (12.5f * windowScale (b), (float) h * 0.46f), true).withExtraKerningFactor (0.06f);
}
juce::Font LookAndFeel::getPopupMenuFont() { return uiFont (14.0f); }

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& t, const juce::String& tooltip)
    : title (t)
{
    setComponentID (paramId);
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityModeParameters (0.4, 1, 0.0, false, juce::ModifierKeys::shiftModifier);
    slider.setTooltip (tooltip);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (s.getParameter (paramId)))
        slider.setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
    setTooltip (tooltip);
}

void Knob::setPrimary (bool p)
{
    slider.getProperties().set ("primary", p);
    resized();
    repaint();
}

float Knob::scale() const { return windowScale (*this); }

int Knob::labelHeight() const { return juce::roundToInt ((isPrimary() ? 20.0f : 17.0f) * scale()); }

void Knob::resized()
{
    // Name, dial and value as one group, centred in the knob's area.
    const int label = labelHeight();
    const int cap = juce::roundToInt ((isPrimary() ? 112.0f : 62.0f) * scale());
    const int d = std::max (0, std::min ({ getWidth(), getHeight() - 2 * label, cap }));
    auto group = getLocalBounds().withSizeKeepingCentre (getWidth(), d + 2 * label);
    group.removeFromTop (label);
    slider.setBounds (group.removeFromTop (d).withSizeKeepingCentre (d, d));
}

void Knob::paint (juce::Graphics& g)
{
    const float s = scale();
    const bool primary = isPrimary();
    const int label = labelHeight();
    const auto dial = slider.getBounds();
    g.setColour (primary ? colours::text : colours::dim);
    g.setFont (uiFont ((primary ? 12.0f : 10.5f) * s, true).withExtraKerningFactor (0.12f));
    g.drawText (title, juce::Rectangle<int> (0, dial.getY() - label, getWidth(), label), juce::Justification::centred);
    g.setColour (colours::text);
    g.setFont (uiFont ((primary ? 14.0f : 12.0f) * s));
    g.drawText (slider.getTextFromValue (slider.getValue()), juce::Rectangle<int> (0, dial.getBottom(), getWidth(), label),
                juce::Justification::centred);
}

//==============================================================================
Segment::Segment (juce::AudioProcessorValueTreeState& s, const juce::String& pid, const juce::StringArray& labels,
                  const juce::String& tooltip)
    : state (s), paramId (pid)
{
    setComponentID (pid);
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->setTooltip (tooltip);
        b->onClick = [this, i, single = labels.size() == 1]
        {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (paramId)))
            {
                // One label: an on/off button for a two-state parameter (index 1 = on).
                const int now = juce::roundToInt (state.getRawParameterValue (paramId)->load());
                const float target = single ? (now == 1 ? 0.0f : 1.0f) : (float) i;
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (target));
                p->endChangeGesture();
            }
        };
        addAndMakeVisible (b);
    }
    state.addParameterListener (paramId, this);
    handleAsyncUpdate();
}

Segment::~Segment() { state.removeParameterListener (paramId, this); }

void Segment::resized()
{
    auto r = getLocalBounds();
    const int w = r.getWidth() / std::max (1, buttons.size());
    for (auto* b : buttons)
        b->setBounds (r.removeFromLeft (w).reduced (2, 0));
}

void Segment::handleAsyncUpdate()
{
    const int index = juce::roundToInt (state.getRawParameterValue (paramId)->load());
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (buttons.size() == 1 ? index == 1 : i == index, juce::dontSendNotification);
}

//==============================================================================
void Meter::setLevels (float l, float r)
{
    const float in[2] = { l, r };
    for (int c = 0; c < 2; ++c)
    {
        levels[c] = std::max (in[c], levels[c] * 0.82f);
        if (in[c] >= held[c]) { held[c] = in[c]; holdFrames[c] = 45; }
        else if (--holdFrames[c] <= 0) held[c] = std::max (0.0f, held[c] - 0.02f);
    }
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float bw = (r.getWidth() - 4.0f) / 2.0f;
    auto toY = [&r] (float lin)
    {
        const float dbv = juce::jlimit (-48.0f, 0.0f, juce::Decibels::gainToDecibels (lin, -48.0f));
        return r.getBottom() - r.getHeight() * (dbv + 48.0f) / 48.0f;
    };
    for (int c = 0; c < 2; ++c)
    {
        auto bar = juce::Rectangle<float> (r.getX() + (float) c * (bw + 4.0f), r.getY(), bw, r.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setGradientFill (juce::ColourGradient (colours::accent, 0, r.getY(), colours::text.withAlpha (0.85f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (bar.withTop (toY (levels[c])), 2.0f);
        g.setColour (held[c] >= 0.999f ? colours::hot : colours::text);
        g.fillRect (bar.getX(), toY (held[c]) - 1.0f, bar.getWidth(), 2.0f);
    }
}

//==============================================================================
GainMeter::GainMeter()
{
    setTooltip (ru ("Насколько полоса сейчас сжата (минус, голубой) или поднята UPWARD (плюс, белый)"));
}

void GainMeter::setValues (float reductionDb, float boostDb)
{
    // Instant rise, about 18 dB/s fall (30 frames per second), so short hits stay readable.
    const float r = std::max (reductionDb, reduction - 0.6f), b = std::max (boostDb, boost - 0.6f);
    if (std::abs (r - reduction) > 0.01f || std::abs (b - boost) > 0.01f)
    {
        reduction = std::max (0.0f, r);
        boost = std::max (0.0f, b);
        repaint();
    }
}

void GainMeter::paint (juce::Graphics& g)
{
    const float s = windowScale (*this);
    auto r = getLocalBounds().toFloat();
    auto textArea = r.removeFromRight (64.0f * s);
    auto track = r.reduced (0.0f, r.getHeight() * 0.28f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
    const bool down = reduction >= boost;
    const float value = down ? reduction : boost;
    if (value > 0.05f)
    {
        g.setColour (down ? colours::accent : colours::text.withAlpha (0.85f));
        g.fillRoundedRectangle (track.withWidth (std::max (track.getHeight(), track.getWidth() * std::min (value, 24.0f) / 24.0f)),
                                track.getHeight() * 0.5f);
    }
    g.setFont (uiFont (12.0f * s, true));
    g.setColour (value > 0.05f ? (down ? colours::accent : colours::text) : colours::faint);
    const juce::String txt = value > 0.05f ? juce::String (down ? "-" : "+") + juce::String (value, 1) + " dB" : juce::String ("0.0 dB");
    g.drawText (txt, textArea, juce::Justification::centredRight);
}

//==============================================================================
BandDisplay::BandDisplay (PowerCompProcessor& p) : proc (p)
{
    setComponentID ("display");
    inDb.fill (-120.0f);
    outDb.fill (-120.0f);
    setTooltip (ru ("Спектр: серый — вход, голубой — выход. Линии между полосами можно тянуть мышью, двойной щелчок — по умолчанию"));
}

juce::Rectangle<float> BandDisplay::plotArea() const
{
    const float s = windowScale (*this);
    return getLocalBounds().toFloat().reduced (12.0f * s, 8.0f * s).withTrimmedBottom (14.0f * s);
}

float BandDisplay::xForFrequency (float hz) const
{
    const auto plot = plotArea();
    return plot.getX() + plot.getWidth() * (float) (std::log (std::max (hz, 1.0f) / 20.0f) / std::log (1000.0f));
}

float BandDisplay::frequencyForX (float x) const
{
    const auto plot = plotArea();
    return 20.0f * std::pow (1000.0f, (x - plot.getX()) / std::max (1.0f, plot.getWidth()));
}

juce::RangedAudioParameter* BandDisplay::splitParam (int which) const
{
    return dynamic_cast<juce::RangedAudioParameter*> (proc.state.getParameter (which == 0 ? pc::id::lowSplit : pc::id::highSplit));
}

int BandDisplay::splitAt (float x) const
{
    int best = -1;
    float bestDist = 7.0f * windowScale (*this);
    for (int which = 0; which < 2; ++which)
    {
        const float hz = proc.state.getRawParameterValue (which == 0 ? pc::id::lowSplit : pc::id::highSplit)->load();
        const float d = std::abs (x - xForFrequency (hz));
        if (d < bestDist)
        {
            bestDist = d;
            best = which;
        }
    }
    return best;
}

void BandDisplay::computeColumns (const std::array<float, PowerCompProcessor::ringSize>& ring, int write, std::array<float, columns>& out)
{
    const int start = (write - fftSize) & (PowerCompProcessor::ringSize - 1);
    std::fill (scratch.begin(), scratch.end(), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        scratch[(size_t) i] = ring[(size_t) ((start + i) & (PowerCompProcessor::ringSize - 1))];
    window.multiplyWithWindowingTable (scratch.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (scratch.data(), true);
    const double sr = proc.currentRate.load();
    const int maxBin = fftSize / 2 - 1;
    for (int c = 0; c < columns; ++c)
    {
        const double f0 = 20.0 * std::pow (1000.0, (double) c / columns), f1 = 20.0 * std::pow (1000.0, (double) (c + 1) / columns);
        const double k0 = f0 * fftSize / sr, k1 = f1 * fftSize / sr;
        float mag = 0.0f;
        if (k1 - k0 < 1.0) // fewer than one bin per column: interpolate at the centre
        {
            const double k = juce::jlimit (0.0, (double) maxBin - 1.0, 0.5 * (k0 + k1));
            const int i = (int) k;
            const float t = (float) (k - i);
            mag = scratch[(size_t) i] * (1.0f - t) + scratch[(size_t) i + 1] * t;
        }
        else
            for (int i = juce::jlimit (0, maxBin, (int) k0); i <= juce::jlimit (0, maxBin, (int) std::ceil (k1)); ++i)
                mag = std::max (mag, scratch[(size_t) i]);
        // Amplitude (the window sums to its length), tilted 3 dB per octave around 1 kHz so music looks level.
        const float fc = (float) std::sqrt (f0 * f1);
        const float level = juce::Decibels::gainToDecibels (mag * 2.0f / fftSize, -120.0f) + 3.0f * std::log2 (fc / 1000.0f);
        out[(size_t) c] = std::max (level, out[(size_t) c] - 1.2f);
    }
}

void BandDisplay::updateSpectrum()
{
    const int w = proc.ringWrite.load (std::memory_order_acquire);
    if (w == lastWrite) // no audio: fall away
    {
        for (auto* a : { &inDb, &outDb })
            for (auto& v : *a)
                v = std::max (-120.0f, v - 1.2f);
    }
    else
    {
        lastWrite = w;
        computeColumns (proc.inRing, w, inDb);
        computeColumns (proc.outRing, w, outDb);
    }
    repaint();
}

void BandDisplay::paint (juce::Graphics& g)
{
    const float s = windowScale (*this);
    auto r = getLocalBounds().toFloat();
    fillPanel (g, r, 10.0f * s);
    const auto plot = plotArea();

    // Frequency grid and labels.
    g.setFont (uiFont (9.5f * s, true));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFrequency (f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (colours::faint);
        const juce::String label = f < 1000.0f ? juce::String ((int) f) : juce::String ((int) (f / 1000.0f)) + "k";
        g.drawText (label, juce::Rectangle<float> (x - 20.0f * s, plot.getBottom() + 1.0f, 40.0f * s, 13.0f * s), juce::Justification::centred);
    }

    // Bands: solo and bypass shade them.
    const float split[2] = { proc.state.getRawParameterValue (pc::id::lowSplit)->load(), proc.state.getRawParameterValue (pc::id::highSplit)->load() };
    const float xs[4] = { plot.getX(), xForFrequency (split[0]), xForFrequency (split[1]), plot.getRight() };
    bool soloed[pc::numBands], bypassed[pc::numBands], anySolo = false;
    for (int b = 0; b < pc::numBands; ++b)
    {
        soloed[b] = proc.state.getRawParameterValue (pc::id::solo[b])->load() > 0.5f;
        bypassed[b] = proc.state.getRawParameterValue (pc::id::bypass[b])->load() > 0.5f;
        anySolo = anySolo || soloed[b];
    }
    for (int b = 0; b < pc::numBands; ++b)
    {
        const auto zone = juce::Rectangle<float>::leftTopRightBottom (xs[b], plot.getY(), xs[b + 1], plot.getBottom());
        if (soloed[b])
            g.setColour (colours::accent.withAlpha (0.07f));
        else if ((anySolo && ! soloed[b]) || bypassed[b])
            g.setColour (juce::Colours::black.withAlpha (0.28f));
        else
            continue;
        g.fillRect (zone);
    }

    // Spectrum: input filled grey, output as an accent line.
    auto pathFor = [&] (const std::array<float, columns>& v, bool closed)
    {
        juce::Path p;
        auto y = [&] (float dbv) { return juce::jmap (juce::jlimit (-90.0f, 6.0f, dbv), -90.0f, 6.0f, plot.getBottom(), plot.getY() + 14.0f * s); };
        for (int c = 0; c < columns; ++c)
        {
            const float x = plot.getX() + plot.getWidth() * ((float) c + 0.5f) / columns;
            if (c == 0) closed ? (p.startNewSubPath (plot.getX(), plot.getBottom()), p.lineTo (x, y (v[0]))) : p.startNewSubPath (x, y (v[0]));
            else p.lineTo (x, y (v[(size_t) c]));
        }
        if (closed)
        {
            p.lineTo (plot.getRight(), plot.getBottom());
            p.closeSubPath();
        }
        return p;
    };
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillPath (pathFor (inDb, true));
        g.setColour (colours::accent.withAlpha (0.10f));
        g.fillPath (pathFor (outDb, true));
        g.setColour (colours::accent.withAlpha (0.9f));
        g.strokePath (pathFor (outDb, false), juce::PathStrokeType (1.5f * s));
    }

    // Band names at the top of each zone.
    g.setFont (uiFont (11.0f * s, true).withExtraKerningFactor (0.16f));
    for (int b = 0; b < pc::numBands; ++b)
    {
        const auto zone = juce::Rectangle<float>::leftTopRightBottom (xs[b], plot.getY(), xs[b + 1], plot.getY() + 14.0f * s);
        if (zone.getWidth() < 34.0f * s)
            continue;
        juce::String name (bandTitles[b]);
        if (bypassed[b]) name << "  BYPASS";
        g.setColour (soloed[b] ? colours::accent : (bypassed[b] || (anySolo && ! soloed[b]) ? colours::faint : colours::dim));
        g.drawText (name, zone, juce::Justification::centred);
    }

    // Crossover lines with their frequency.
    for (int which = 0; which < 2; ++which)
    {
        const float x = xs[which + 1];
        const bool active = hover == which || dragging == which;
        g.setColour (active ? colours::accent : colours::text.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (x - 0.75f * s, plot.getY(), 1.5f * s, plot.getHeight()));
        const auto handle = juce::Rectangle<float> (64.0f * s, 18.0f * s).withCentre ({ x, plot.getBottom() - 14.0f * s });
        g.setColour (active ? colours::accent : juce::Colour (0xff2a2d33));
        g.fillRoundedRectangle (handle, 9.0f * s);
        g.setColour (active ? colours::accent : colours::text.withAlpha (0.35f));
        g.drawRoundedRectangle (handle, 9.0f * s, 1.0f);
        g.setColour (active ? colours::ink : colours::text);
        g.setFont (uiFont (10.5f * s, true));
        g.drawText (pc::hzText (split[which]), handle, juce::Justification::centred);
    }

    g.setFont (uiFont (10.0f * s, true).withExtraKerningFactor (0.1f));
    const auto legend = getLocalBounds().toFloat().reduced (12.0f * s, 8.0f * s).removeFromTop (14.0f * s).removeFromRight (90.0f * s);
    g.setColour (colours::dim);
    g.drawText ("IN", legend.withTrimmedRight (40.0f * s), juce::Justification::centredRight);
    g.setColour (colours::accent);
    g.drawText ("OUT", legend, juce::Justification::centredRight);
}

void BandDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = splitAt ((float) e.x);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void BandDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hover >= 0 && dragging < 0)
    {
        hover = -1;
        repaint();
    }
}

void BandDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = splitAt ((float) e.x);
    if (dragging >= 0)
        if (auto* p = splitParam (dragging))
            p->beginChangeGesture();
}

void BandDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    if (auto* p = splitParam (dragging))
    {
        const auto range = p->getNormalisableRange();
        const float hz = juce::jlimit (range.start, range.end, frequencyForX ((float) e.x));
        p->setValueNotifyingHost (p->convertTo0to1 (hz));
        repaint();
    }
}

void BandDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        if (auto* p = splitParam (dragging))
            p->endChangeGesture();
    dragging = -1;
    repaint();
}

void BandDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int which = splitAt ((float) e.x);
    if (auto* p = which >= 0 ? splitParam (which) : nullptr)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
        repaint();
    }
}

//==============================================================================
AdvancedPanel::AdvancedPanel (juce::AudioProcessorValueTreeState& s)
    : autoGain (s, pc::id::autoGain, { "AUTO GAIN" },
                ru ("Компенсация громкости: COMPRESS делает звук ровнее, а не тише. Выключено — обычный компрессор, "
                    "громкость выставляют GAIN и OUTPUT")),
      link (s, pc::id::link, "STEREO LINK",
            ru ("Одинаковое сжатие левого и правого канала: 100 % — стереокартина не плывёт, меньше — каналы сжимаются отдельно"))
{
    addAndMakeVisible (autoGain);
    addAndMakeVisible (link);
}

void AdvancedPanel::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / 520.0f;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    fillOverlay (g, getLocalBounds().toFloat());
    g.setFont (uiFont (11.0f * s, true).withExtraKerningFactor (0.18f));
    g.setColour (colours::dim);
    g.drawText ("ADVANCED", getLocalBounds().reduced (S (16), S (10)).removeFromTop (S (16)), juce::Justification::centredLeft);
    g.setFont (uiFont (10.5f * s, true).withExtraKerningFactor (0.1f));
    g.setColour (colours::faint);
    g.drawText ("COMPRESS KEEPS THE LOUDNESS", juce::Rectangle<int> (S (16), S (86), S (260), S (14)), juce::Justification::centredLeft);
}

void AdvancedPanel::resized()
{
    const float s = (float) getWidth() / 520.0f;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    autoGain.setBounds (S (14), S (50), S (170), S (28));
    link.setBounds (S (330), S (24), S (150), S (116));
}

//==============================================================================
namespace
{
const juce::String userCategory { "User" };

void fillSelection (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (colours::accent);
    g.fillRoundedRectangle (r, radius);
}

const juce::Colour selectedText = colours::ink;
}

PresetBrowser::PresetBrowser (PowerCompProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    search.setTextToShowWhenEmpty ("Search presets", colours::dim);
    search.setJustification (juce::Justification::centredLeft);
    search.setTooltip (ru ("Поиск по названию, категории или силе сжатия"));
    search.onTextChange = [this] { refilter(); };
    search.onReturnKey = [this] { close(); };
    search.onEscapeKey = [this] { close(); };
    search.onKey = [this] (const juce::KeyPress& k)
    {
        return (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey) && keyPressed (k);
    };
    closeButton.setTooltip (ru ("Назад к регуляторам (Enter или Esc)"));
    closeButton.onClick = [this] { close(); };
    allTypesButton.setTooltip (ru ("Показать пресеты любой силы"));
    allTypesButton.onClick = [this] { selectedGroups.clear(); refilter(); };
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, colours::faint);
    list.addMouseListener (this, true);
    for (auto* c : std::initializer_list<juce::Component*> { &search, &closeButton, &allTypesButton, &categories, &list })
        addAndMakeVisible (c);
}

PresetBrowser::~PresetBrowser()
{
    list.removeMouseListener (this);
}

void PresetBrowser::open()
{
    proc.library.rescan();
    categoryNames.clear();
    for (const auto& p : proc.library.all())
        if (p.category != userCategory)
            categoryNames.addIfNotAlreadyThere (p.category);
    if (category.isNotEmpty() && category != userCategory && ! categoryNames.contains (category))
        category = {};
    rebuildGroups();
    refilter();
    setVisible (true);
    if (isShowing())
        search.grabKeyboardFocus();
}

void PresetBrowser::setCategory (const juce::String& c)
{
    category = c;
    refilter();
}

void PresetBrowser::toggleGroup (const juce::String& g)
{
    if (selectedGroups.contains (g))
        selectedGroups.removeString (g);
    else
        selectedGroups.add (g);
    refilter();
}

void PresetBrowser::setSearch (const juce::String& t)
{
    search.setText (t, false);
    refilter();
}

void PresetBrowser::rebuildGroups()
{
    groupNames.clear();
    for (const auto& p : proc.library.all())
        if (p.group.isNotEmpty())
            groupNames.addIfNotAlreadyThere (p.group);
    if (groupButtons.size() == groupNames.size())
        return;
    groupButtons.clear();
    for (const auto& g : groupNames)
    {
        auto* b = groupButtons.add (new juce::TextButton());
        b->setTooltip (ru ("Показать пресеты этой силы (Light — мягко, Medium — средне, Heavy — сильно); можно несколько"));
        b->onClick = [this, g] { toggleGroup (g); };
        addAndMakeVisible (b);
    }
    resized();
}

void PresetBrowser::refilter()
{
    const auto& all = proc.library.all();
    shown = pc::filterPresets (all, category, selectedGroups, search.getText());
    const auto inCategory = pc::filterPresets (all, category, {}, {});
    for (int i = 0; i < groupNames.size(); ++i)
    {
        int n = 0;
        for (int k : inCategory)
            n += all[(size_t) k].group == groupNames[i] ? 1 : 0;
        auto* b = groupButtons[i];
        const bool on = selectedGroups.contains (groupNames[i]);
        b->setButtonText (groupNames[i].toUpperCase() + "  " + juce::String (n));
        b->setToggleState (on, juce::dontSendNotification);
        b->setEnabled (n > 0 || on);
    }
    allTypesButton.setToggleState (selectedGroups.isEmpty(), juce::dontSendNotification);
    resized();
    hoverRow = -1;
    list.updateContent();
    selectCurrent();
    categories.repaint();
    repaint();
}

void PresetBrowser::selectCurrent()
{
    const auto& all = proc.library.all();
    const auto name = proc.getPresetName();
    int row = -1;
    for (int r = 0; r < numShown() && row < 0; ++r)
        if (all[(size_t) shown[(size_t) r]].name == name)
            row = r;
    const juce::ScopedValueSetter<bool> guard (updating, true);
    if (row >= 0)
        list.selectRow (row);
    else
    {
        list.deselectAllRows();
        list.scrollToEnsureRowIsOnscreen (0);
    }
}

void PresetBrowser::choose (int row, bool closeAfter)
{
    if (row < 0 || row >= numShown())
        return;
    proc.loadPreset (shown[(size_t) row]);
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        list.selectRow (row);
    }
    if (onLoaded)
        onLoaded();
    if (closeAfter)
        close();
}

void PresetBrowser::selectedRowsChanged (int lastRow)
{
    if (! updating && lastRow >= 0)
        choose (lastRow, false);
}

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey || k == juce::KeyPress::returnKey)
    {
        close();
        return true;
    }
    if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
    {
        if (shown.empty())
            return true;
        const int current = list.getSelectedRow();
        const int next = current < 0 ? 0 : juce::jlimit (0, numShown() - 1, current + (k == juce::KeyPress::downKey ? 1 : -1));
        if (next != current)
            choose (next, false);
        list.scrollToEnsureRowIsOnscreen (next);
        return true;
    }
    return false;
}

void PresetBrowser::mouseMove (const juce::MouseEvent& e)
{
    int row = -1;
    if (e.eventComponent == &list || list.isParentOf (e.eventComponent))
        row = list.getRowContainingPosition (e.getEventRelativeTo (&list).x, e.getEventRelativeTo (&list).y);
    if (row != hoverRow)
    {
        list.repaintRow (hoverRow);
        hoverRow = row;
        list.repaintRow (hoverRow);
    }
}

void PresetBrowser::mouseExit (const juce::MouseEvent&)
{
    if (hoverRow >= 0)
    {
        list.repaintRow (hoverRow);
        hoverRow = -1;
    }
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= numShown())
        return;
    const auto& p = proc.library.all()[(size_t) shown[(size_t) row]];
    const float s = scale();
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (2.0f * s, 1.5f * s);
    if (selected)
        fillSelection (g, r, 6.0f * s);
    else if (row == hoverRow)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (r, 6.0f * s);
    }
    juce::String meta = p.category == userCategory ? juce::String ("MY PRESET") : p.group.toUpperCase();
    if (category.isEmpty() && p.category != userCategory)
        meta = p.category.toUpperCase() + (meta.isNotEmpty() ? "   " + meta : juce::String());
    auto text = r.reduced (10.0f * s, 0.0f);
    const auto metaFont = uiFont (10.5f * s, true).withExtraKerningFactor (0.08f);
    const float metaW = (float) juce::GlyphArrangement::getStringWidthInt (metaFont, meta);
    g.setFont (metaFont);
    g.setColour (selected ? selectedText.withAlpha (0.65f) : colours::dim);
    g.drawText (meta, text.removeFromRight (metaW), juce::Justification::centredRight, false);
    text.removeFromRight (12.0f * s);
    g.setFont (uiFont (14.0f * s));
    g.setColour (selected ? selectedText : colours::text);
    g.drawText (p.name, text, juce::Justification::centredLeft, true);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    const float s = scale();
    fillOverlay (g, getLocalBounds().toFloat());
    g.setFont (uiFont (11.0f * s, true).withExtraKerningFactor (0.18f));
    g.setColour (colours::text);
    g.drawText ("PRESETS", titleArea, juce::Justification::centredLeft);
    g.setColour (colours::line);
    g.fillRect (juce::Rectangle<int> (categories.getRight() + juce::roundToInt (8 * s), categories.getY(), 1, categories.getHeight()));

    if (shown.empty())
    {
        const bool noOwn = category == userCategory && search.getText().trim().isEmpty() && selectedGroups.isEmpty();
        g.setColour (colours::dim);
        g.setFont (uiFont (14.0f * s));
        g.drawFittedText (noOwn ? "No presets of your own yet.\nSave one with SAVE at the top."
                                : "No presets match.\nClear the search or choose another filter.",
                          emptyArea, juce::Justification::centred, 2);
    }
}

void PresetBrowser::paintOverChildren (juce::Graphics& g)
{
    // Magnifier drawn inside the search field.
    const float s = scale();
    const auto b = search.getBounds().toFloat();
    const float d = 10.0f * s;
    const juce::Point<float> c (b.getX() + 15.0f * s, b.getCentreY() - 1.0f * s);
    g.setColour (colours::dim);
    g.drawEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d, 1.4f * s);
    const auto from = c.getPointOnCircumference (d * 0.5f, juce::MathConstants<float>::pi * 0.75f);
    g.drawLine ({ from, from + juce::Point<float> (3.5f * s, 3.5f * s) }, 1.6f * s);
}

void PresetBrowser::resized()
{
    const float s = scale();
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    auto r = getLocalBounds().reduced (S (16), S (12));

    auto top = r.removeFromTop (S (30));
    closeButton.setBounds (top.removeFromRight (S (72)).withSizeKeepingCentre (S (72), S (24)));
    top.removeFromRight (S (8));
    titleArea = top.removeFromLeft (S (200));
    const int searchW = std::min (top.getWidth() - S (16), S (340));
    searchArea = top.removeFromLeft (searchW).withSizeKeepingCentre (searchW, S (28));
    search.setBounds (searchArea);
    const auto font = uiFont (14.0f * s);
    search.setFont (font);
    search.applyFontToAllText (font);
    search.setIndents (S (30), juce::jmax (0, (searchArea.getHeight() - juce::roundToInt (font.getHeight())) / 2));
    r.removeFromTop (S (12));

    auto left = r.removeFromLeft (S (230));
    r.removeFromLeft (S (17));

    const int pillH = S (26), gap = S (6);
    auto pillWidth = [&] (juce::TextButton& b)
    {
        auto& lf = getLookAndFeel();
        return juce::GlyphArrangement::getStringWidthInt (lf.getTextButtonFont (b, pillH), b.getButtonText()) + S (20);
    };
    std::vector<juce::TextButton*> pills { &allTypesButton };
    for (auto* b : groupButtons)
        pills.push_back (b);
    int x = r.getX(), y = r.getY();
    for (auto* b : pills)
    {
        const int w = pillWidth (*b);
        if (x > r.getX() && x + w > r.getRight())
        {
            x = r.getX();
            y += pillH + gap;
        }
        b->setBounds (x, y, w, pillH);
        x += w + gap;
    }
    r.setTop (y + pillH + S (12));
    list.setRowHeight (S (28));
    list.setBounds (r);
    emptyArea = r.withHeight (S (90));
    categories.setBounds (left);
}

int PresetBrowser::Categories::rowAt (int y) const
{
    const float s = owner.scale();
    const int rowH = juce::roundToInt (30 * s), n = owner.categoryNames.size() + 1;
    if (y < rowH * n)
        return y / rowH;
    const int userTop = rowH * n + juce::roundToInt (14 * s);
    return y >= userTop && y < userTop + rowH ? n : -1;
}

void PresetBrowser::Categories::paint (juce::Graphics& g)
{
    const float s = owner.scale();
    const int rowH = juce::roundToInt (30 * s), n = owner.categoryNames.size() + 1;
    const auto& all = owner.proc.library.all();
    for (int i = 0; i <= n; ++i)
    {
        const bool user = i == n;
        const juce::String key = i == 0 ? juce::String() : (user ? userCategory : owner.categoryNames[i - 1]);
        const juce::String label = i == 0 ? juce::String ("All presets") : (user ? juce::String ("My presets") : key);
        const int top = user ? rowH * n + juce::roundToInt (14 * s) : rowH * i;
        if (user)
        {
            g.setColour (colours::line);
            g.fillRect (juce::Rectangle<int> (0, rowH * n + juce::roundToInt (7 * s), getWidth(), 1));
        }
        auto r = juce::Rectangle<float> (0.0f, (float) top, (float) getWidth(), (float) rowH).reduced (0.0f, 1.5f * s);
        const bool selected = owner.category == key;
        if (selected)
            fillSelection (g, r, 6.0f * s);
        else if (i == hover)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (r, 6.0f * s);
        }
        auto text = r.reduced (10.0f * s, 0.0f);
        const auto count = juce::String ((int) pc::filterPresets (all, key, {}, {}).size());
        g.setFont (uiFont (12.0f * s, true));
        g.setColour (selected ? selectedText.withAlpha (0.65f) : colours::dim);
        g.drawText (count, text.removeFromRight (34.0f * s), juce::Justification::centredRight);
        g.setFont (uiFont (13.5f * s));
        g.setColour (selected ? selectedText : colours::text);
        g.drawText (label, text, juce::Justification::centredLeft, true);
    }
}

void PresetBrowser::Categories::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (row != hover)
    {
        hover = row;
        repaint();
    }
}

void PresetBrowser::Categories::mouseDown (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y), n = owner.categoryNames.size() + 1;
    if (row < 0)
        return;
    owner.setCategory (row == 0 ? juce::String() : (row == n ? userCategory : owner.categoryNames[row - 1]));
}

//==============================================================================
PowerCompEditor::PowerCompEditor (PowerCompProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      lowSplit (p.state, pc::id::lowSplit, "LOW | MID", ru ("Граница между низом и серединой. Её же можно тянуть мышью на дисплее")),
      highSplit (p.state, pc::id::highSplit, "MID | HIGH", ru ("Граница между серединой и верхом. Её же можно тянуть мышью на дисплее")),
      speed (p.state, pc::id::speed, "SPEED",
             ru ("Скорость реакции. Влево — медленно: начало удара проходит, больше панча. Вправо — быстро: плотнее и ровнее")),
      upward (p.state, pc::id::upward, "UPWARD",
              ru ("Подтягивает тихие хвосты и детали (как OTT). Тишину и шум ниже −72 дБ не поднимает")),
      mix (p.state, pc::id::mix, "MIX",
           ru ("Смесь с исходным звуком: 100 % — только сжатый, меньше — параллельная компрессия (удар остаётся живым)")),
      output (p.state, pc::id::output, "OUTPUT", ru ("Общая громкость на выходе")),
      analogMode (p.state, pc::id::analog, { "DIGITAL", "ANALOGUE" },
                  ru ("DIGITAL — чистое сжатие. ANALOGUE — тёплое мягкое насыщение низа и середины. Смена пресета его не меняет")),
      display (p), advanced (p.state), browser (p)
{
    setLookAndFeel (&lnf);
    for (int b = 0; b < pc::numBands; ++b)
    {
        const auto bi = (size_t) b;
        compress[bi] = std::make_unique<pc::ui::Knob> (p.state, pc::id::comp[b], "COMPRESS",
                                                       ru ("Сколько сжимать эту полосу: 0 — не трогать, больше — ровнее и плотнее. "
                                                           "AUTO GAIN держит громкость. Shift — точно, двойной щелчок — сброс"));
        compress[bi]->setPrimary (true);
        gain[bi] = std::make_unique<pc::ui::Knob> (p.state, pc::id::gain[b], "GAIN", ru ("Громкость этой полосы после сжатия (±12 дБ)"));
        gain[bi]->slider.getProperties().set ("bipolar", true);
        solo[bi] = std::make_unique<pc::ui::Segment> (p.state, pc::id::solo[b], juce::StringArray { "SOLO" },
                                                      ru ("Слушать только эту полосу (можно включить несколько)"));
        bypass[bi] = std::make_unique<pc::ui::Segment> (p.state, pc::id::bypass[b], juce::StringArray { "BYPASS" },
                                                        ru ("Оставить эту полосу без обработки"));
        gainMeters[bi] = std::make_unique<pc::ui::GainMeter>();
        gainMeters[bi]->setComponentID ("meter" + juce::String (b));
        for (juce::Component* c : { (juce::Component*) compress[bi].get(), (juce::Component*) gain[bi].get(), (juce::Component*) solo[bi].get(),
                                    (juce::Component*) bypass[bi].get(), (juce::Component*) gainMeters[bi].get() })
            addAndMakeVisible (c);
    }
    output.setPrimary (true);
    output.slider.getProperties().set ("bipolar", true);
    analogMode.setComponentID ("analog");
    for (auto* c : std::initializer_list<juce::Component*> { &lowSplit, &highSplit, &speed, &upward, &mix, &output, &analogMode, &display,
                                                             &meter, &prevButton, &nextButton, &presetButton, &saveButton, &advancedButton,
                                                             &menuButton })
        addAndMakeVisible (c);
    meter.setComponentID ("outputMeter");
    addChildComponent (advanced);
    addChildComponent (browser);
    presetButton.setComponentID ("presetButton");
    browser.setComponentID ("presetBrowser");
    advanced.setComponentID ("advancedPanel");
    advancedButton.setComponentID ("advancedButton");

    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    presetButton.onClick = [this]
    {
        if (browser.isVisible())
        {
            browser.onClose();
            return;
        }
        if (advancedButton.getToggleState())
            advancedButton.setToggleState (false, juce::sendNotification);
        presetButton.setToggleState (true, juce::dontSendNotification);
        browser.open();
    };
    browser.onLoaded = [this] { refreshHeader(); };
    browser.onClose = [this]
    {
        browser.setVisible (false);
        presetButton.setToggleState (false, juce::dontSendNotification);
    };
    saveButton.onClick = [this] { savePreset(); };
    menuButton.onClick = [this] { showMainMenu(); };
    advancedButton.setClickingTogglesState (true);
    advancedButton.onClick = [this]
    {
        if (advancedButton.getToggleState() && browser.isVisible())
            browser.onClose();
        advanced.setVisible (advancedButton.getToggleState());
    };
    presetButton.setTooltip (ru ("Пресеты: щелчок — список по задачам (мастер, барабаны, бас, синты, вокал)"));
    prevButton.setTooltip (ru ("Предыдущий пресет"));
    nextButton.setTooltip (ru ("Следующий пресет"));
    saveButton.setTooltip (ru ("Сохранить текущую настройку как свой пресет"));
    advancedButton.setTooltip (ru ("Дополнительно: AUTO GAIN и STEREO LINK"));
    menuButton.setTooltip (ru ("Переименовать, удалить, импорт и экспорт пресетов"));

    setResizable (true, true);
    setResizeLimits (864, 576, 1620, 1080);
    getConstrainer()->setFixedAspectRatio (1080.0 / 720.0);
    setSize (1080, 720);
    refreshHeader();
    startTimerHz (30);
}

PowerCompEditor::~PowerCompEditor()
{
    setLookAndFeel (nullptr);
}

juce::String PowerCompEditor::bandRange (int band) const
{
    const auto lo = pc::hzText (proc.state.getRawParameterValue (pc::id::lowSplit)->load());
    const auto hi = pc::hzText (proc.state.getRawParameterValue (pc::id::highSplit)->load());
    if (band == 0) return "BELOW " + lo.toUpperCase();
    if (band == 1) return lo.toUpperCase() + " - " + hi.toUpperCase();
    return "ABOVE " + hi.toUpperCase();
}

void PowerCompEditor::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (colours::bgTop, 0, 0, colours::bgBottom, 0, r.getHeight(), false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x12e6f6ff), r.getWidth() * 0.15f, 0.0f,
                                             juce::Colour (0x00e6f6ff), r.getWidth() * 0.6f, r.getHeight() * 0.6f, true));
    g.fillAll();

    const float s = (float) getWidth() / 1080.0f;
    auto header = getLocalBounds().removeFromTop (juce::roundToInt (60 * s)).reduced (juce::roundToInt (24 * s), 0);
    const auto wordFont = uiFont (19.0f * s, true).withExtraKerningFactor (0.08f);
    g.setFont (wordFont);
    g.setColour (colours::text);
    g.drawText ("POWER", header, juce::Justification::centredLeft);
    g.setColour (colours::accent);
    g.drawText ("COMP", header.withTrimmedLeft (juce::GlyphArrangement::getStringWidthInt (wordFont, "POWER") + 2), juce::Justification::centredLeft);
    g.setColour (colours::line);
    g.drawHorizontalLine (juce::roundToInt (60 * s), 0.0f, (float) getWidth());

    for (const auto& sec : sections)
    {
        fillPanel (g, sec.area.toFloat(), 12.0f * s);
        auto titleRow = sec.area.reduced (juce::roundToInt (16 * s), juce::roundToInt (10 * s)).removeFromTop (juce::roundToInt (18 * s));
        g.setFont (uiFont (12.0f * s, true).withExtraKerningFactor (0.2f));
        g.setColour (colours::text);
        g.drawText (sec.title, titleRow, juce::Justification::centredLeft);
        if (sec.band >= 0) // the frequencies this band covers, on the right of its title
        {
            g.setFont (uiFont (10.0f * s, true).withExtraKerningFactor (0.12f));
            g.setColour (colours::dim);
            g.drawText (bandRange (sec.band), titleRow, juce::Justification::centredRight);
        }
    }
}

void PowerCompEditor::resized()
{
    const float s = (float) getWidth() / 1080.0f;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    sections.clear();

    // Header: logo and DIGITAL | ANALOGUE on the left, the preset in the centre, ADVANCED and the menu on the right.
    auto header = getLocalBounds().removeFromTop (S (60)).reduced (S (24), S (14));
    menuButton.setBounds (header.removeFromRight (S (40)));
    header.removeFromRight (S (8));
    advancedButton.setBounds (header.removeFromRight (S (96)));
    auto left = getLocalBounds().removeFromTop (S (60)).reduced (S (24), S (14));
    left.removeFromLeft (S (136));
    analogMode.setBounds (left.removeFromLeft (S (164)));
    auto centre = getLocalBounds().removeFromTop (S (60)).withSizeKeepingCentre (S (420), S (32));
    prevButton.setBounds (centre.removeFromLeft (S (32)));
    saveButton.setBounds (centre.removeFromRight (S (64)));
    centre.removeFromRight (S (6));
    nextButton.setBounds (centre.removeFromRight (S (32)));
    presetButton.setBounds (centre.reduced (S (6), 0));

    auto body = getLocalBounds().withTrimmedTop (S (72)).reduced (S (20), 0).withTrimmedBottom (S (16));

    // Display with OUTPUT and its meter on the right (as MASTER in the synths of the series).
    auto displayRow = body.removeFromTop (S (170));
    auto out = displayRow.removeFromRight (S (150));
    displayRow.removeFromRight (S (14));
    display.setBounds (displayRow);
    meter.setBounds (out.removeFromRight (S (14)).reduced (0, S (18)));
    out.removeFromRight (S (6));
    output.setBounds (out);
    body.removeFromTop (S (14));

    auto bottom = body.removeFromBottom (S (160));
    body.removeFromBottom (S (14));
    auto bands = body;
    const int gutter = S (20);
    const int blockW = (bands.getWidth() - 2 * gutter) / 3;
    auto inner = [&] (juce::Rectangle<int> area) { return area.reduced (S (14), S (10)).withTrimmedTop (S (24)); };

    // LOW / MID / HIGH: the meter on top, COMPRESS on the left, GAIN with SOLO and BYPASS on the right.
    for (int b = 0; b < pc::numBands; ++b)
    {
        const auto bi = (size_t) b;
        const auto area = b < 2 ? bands.removeFromLeft (blockW) : bands;
        if (b < 2)
            bands.removeFromLeft (gutter);
        sections.push_back ({ bandTitles[b], area, b });
        auto a = inner (area);
        gainMeters[bi]->setBounds (a.removeFromTop (S (18)));
        a.removeFromTop (S (8));
        compress[bi]->setBounds (a.removeFromLeft (a.getWidth() * 55 / 100));
        a.removeFromLeft (S (6));
        bypass[bi]->setBounds (a.removeFromBottom (S (26)));
        a.removeFromBottom (S (6));
        solo[bi]->setBounds (a.removeFromBottom (S (26)));
        a.removeFromBottom (S (4));
        gain[bi]->setBounds (a);
    }

    // CROSSOVER under LOW, DYNAMICS under MID and HIGH.
    const auto crossArea = bottom.removeFromLeft (blockW);
    bottom.removeFromLeft (gutter);
    const auto dynArea = bottom;
    sections.push_back ({ "CROSSOVER", crossArea });
    sections.push_back ({ "DYNAMICS", dynArea });
    auto c = inner (crossArea);
    lowSplit.setBounds (c.removeFromLeft (c.getWidth() / 2));
    highSplit.setBounds (c);
    auto d = inner (dynArea);
    const int third = d.getWidth() / 3;
    speed.setBounds (d.removeFromLeft (third));
    upward.setBounds (d.removeFromLeft (third));
    mix.setBounds (d);

    advanced.setBounds (getWidth() - S (24) - S (520), S (64), S (520), S (150));
    browser.setBounds (getLocalBounds().withTrimmedTop (S (60)).reduced (S (24), S (14)));
}

void PowerCompEditor::timerCallback()
{
    meter.setLevels (proc.meterL.exchange (0.0f), proc.meterR.exchange (0.0f));
    for (size_t b = 0; b < (size_t) pc::numBands; ++b)
        gainMeters[b]->setValues (proc.reductionDb[b].exchange (0.0f), proc.boostDb[b].exchange (0.0f));
    display.updateSpectrum();
    // The band titles show the crossover frequencies.
    const float lo = proc.state.getRawParameterValue (pc::id::lowSplit)->load(), hi = proc.state.getRawParameterValue (pc::id::highSplit)->load();
    if (! juce::exactlyEqual (lo, shownSplits[0]) || ! juce::exactlyEqual (hi, shownSplits[1]))
    {
        shownSplits[0] = lo;
        shownSplits[1] = hi;
        for (const auto& sec : sections)
            if (sec.band >= 0)
                repaint (sec.area);
    }
}

//==============================================================================
void PowerCompEditor::stepPreset (int delta)
{
    if (browser.isVisible()) // with the browser open, < > step through its filtered list
    {
        browser.keyPressed (juce::KeyPress (delta > 0 ? juce::KeyPress::downKey : juce::KeyPress::upKey));
        return;
    }
    proc.library.rescan();
    const auto order = pc::filterPresets (proc.library.all(), {}, {}, {});
    if (order.empty()) return;
    const int current = proc.library.indexOf (proc.getPresetName());
    const auto at = std::find (order.begin(), order.end(), current);
    const int n = (int) order.size();
    const int pos = at == order.end() ? 0 : ((int) (at - order.begin()) + delta + n) % n;
    proc.loadPreset (order[(size_t) pos]);
    refreshHeader();
}

void PowerCompEditor::savePreset()
{
    askForName ("Save preset", proc.getPresetName(), [this] (juce::String name)
    {
        juce::String e;
        if (! proc.library.saveUser (name, pc::captureSnapshot (proc.state), e)) { showError (e); return; }
        proc.setPresetName (name.trim());
        refreshHeader();
    });
}

void PowerCompEditor::showMainMenu()
{
    const int current = proc.library.indexOf (proc.getPresetName());
    const bool isUser = current >= 0 && ! proc.library.all()[(size_t) current].isFactory();
    juce::PopupMenu menu;
    menu.addItem (1, "Save as...");
    menu.addItem (2, "Rename...", isUser);
    menu.addItem (3, "Delete...", isUser);
    menu.addSeparator();
    menu.addItem (4, "Import preset...");
    menu.addItem (5, "Export preset...");
    menu.addItem (6, "Show preset folder");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton), [this, current] (int r)
    {
        if (r == 1)
            savePreset();
        else if (r == 2)
            askForName ("Rename preset", proc.getPresetName(), [this, current] (juce::String name)
            {
                juce::String e;
                if (! proc.library.rename (current, name, e)) { showError (e); return; }
                proc.setPresetName (name.trim());
                refreshHeader();
            });
        else if (r == 3)
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                                                "Move \"" + proc.getPresetName() + "\" to the Trash?", "Delete", "Cancel", this,
                                                juce::ModalCallbackFunction::create ([this, current] (int ok)
                                                {
                                                    juce::String e;
                                                    if (ok == 1 && ! proc.library.remove (current, e)) showError (e);
                                                    refreshHeader();
                                                }));
        else if (r == 4)
        {
            chooser = std::make_unique<juce::FileChooser> ("Import PowerComp preset", juce::File(), "*.pcpreset");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult() == juce::File()) return;
                                      juce::String name, e;
                                      if (! proc.library.importFile (fc.getResult(), name, e)) { showError (e); return; }
                                      proc.loadPreset (proc.library.indexOf (name));
                                      refreshHeader();
                                  });
        }
        else if (r == 5)
        {
            chooser = std::make_unique<juce::FileChooser> ("Export PowerComp preset",
                                                           juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                               .getChildFile (juce::File::createLegalFileName (proc.getPresetName()) + ".pcpreset"),
                                                           "*.pcpreset");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      if (fc.getResult() == juce::File()) return;
                                      juce::String e;
                                      if (! proc.library.exportTo (fc.getResult().withFileExtension ("pcpreset"), proc.getPresetName(),
                                                                   pc::captureSnapshot (proc.state), e))
                                          showError (e);
                                  });
        }
        else if (r == 6)
        {
            auto dir = pc::PresetLibrary::defaultUserDirectory();
            dir.createDirectory();
            dir.revealToUser();
        }
    });
}

void PowerCompEditor::askForName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, "Preset name", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
    {
        const auto name = w->getTextEditorContents ("name").trim();
        if (r == 1 && name.isNotEmpty()) done (name);
    }), true);
}

void PowerCompEditor::showError (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "PowerComp", message, "OK", this);
}
