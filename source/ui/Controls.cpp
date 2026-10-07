#include "Controls.h"
#include "ConverbLookAndFeel.h"
#include "Palette.h"

namespace
{
    constexpr float meterDecayDbPerFrame = 1.2f; // at ~30 fps
    constexpr int peakHoldFrames = 45;
    constexpr std::array<float, 7> meterScaleDb { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f };
    constexpr std::array<float, 7> faderTicksDb { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f };
}

//==============================================================================
void LevelMeter::setPeaks (std::array<float, 2> linearPeaks)
{
    auto changed = false;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        const auto db = juce::Decibels::gainToDecibels (linearPeaks[ch], minDb);
        const auto next = std::max (db, levelsDb[ch] - meterDecayDbPerFrame);

        if (db >= holdDb[ch])
        {
            holdDb[ch] = db;
            holdFrames[ch] = peakHoldFrames;
        }
        else if (--holdFrames[ch] <= 0)
        {
            holdDb[ch] = std::max (minDb, holdDb[ch] - meterDecayDbPerFrame);
        }

        changed = changed || std::abs (next - levelsDb[ch]) > 0.01f || holdDb[ch] > minDb;
        levelsDb[ch] = next;
    }

    if (changed)
        repaint();
}

float LevelMeter::dbToY (float db) const
{
    const auto proportion = juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, 0.0f, 1.0f);
    return (float) getHeight() * (1.0f - proportion);
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto barWidth = (bounds.getWidth() - 2.0f) * 0.5f;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        const auto bar = juce::Rectangle<float> (bounds.getX() + (float) ch * (barWidth + 2.0f), bounds.getY(), barWidth, bounds.getHeight());

        g.setColour (Palette::panelInset);
        g.fillRoundedRectangle (bar, 1.5f);

        const auto top = dbToY (levelsDb[ch]);

        if (levelsDb[ch] > minDb)
        {
            const auto lit = bar.withTop (top);
            g.setGradientFill (juce::ColourGradient (Palette::accent, bar.getX(), dbToY (0.0f), Palette::accentDeep.withAlpha (0.8f), bar.getX(), bar.getBottom(), false));
            g.fillRect (lit);
        }

        if (holdDb[ch] > minDb)
        {
            g.setColour (holdDb[ch] > 0.0f ? Palette::error : Palette::accentSoft);
            g.fillRect (bar.withTop (dbToY (holdDb[ch])).withHeight (1.5f));
        }
    }
}

//==============================================================================
void configureSlider (juce::Slider& slider, juce::RangedAudioParameter& parameter)
{
    const auto defaultValue = parameter.convertFrom0to1 (parameter.getDefaultValue());
    slider.setDoubleClickReturnValue (true, defaultValue);
    slider.setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    slider.setTooltip ("Double-click to reset, shift-drag for fine control");
}

//==============================================================================
GainStrip::GainStrip (const juce::String& titleIn, juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, bool meterOnLeftIn)
    : title (titleIn), meterOnLeft (meterOnLeftIn), attachment (state, parameterID, fader)
{
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 26);
    fader.setSliderSnapsToMousePosition (false);
    configureSlider (fader, *state.getParameter (parameterID));
    fader.setTitle (title + " gain");

    addAndMakeVisible (fader);
    addAndMakeVisible (meter);
}

void GainStrip::paint (juce::Graphics& g)
{
    ConverbLookAndFeel::drawPanel (g, getLocalBounds().toFloat());

    g.setColour (Palette::textLabel);
    g.setFont (ConverbLookAndFeel::titleFont (15.0f, 0.4f));
    g.drawText (title, getLocalBounds().withHeight (44).withTrimmedTop (18), juce::Justification::centred);
    g.setColour (Palette::textAxis);
    g.setFont (ConverbLookAndFeel::titleFont (10.5f, 0.25f));
    g.drawText ("GAIN", getLocalBounds().withHeight (60).withTrimmedTop (42), juce::Justification::centred);

    // Meter scale
    g.setFont (ConverbLookAndFeel::valueFont (10.0f));
    for (auto db : meterScaleDb)
    {
        const auto y = (float) meter.getY() + meter.dbToY (db);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> ((float) scaleArea.getX(), y - 6.0f, (float) scaleArea.getWidth(), 12.0f), meterOnLeft ? juce::Justification::centredRight : juce::Justification::centredLeft);
    }

    // Fader ticks, positioned by the slider's own value->pixel mapping
    const auto centreX = (float) fader.getBounds().getCentreX();
    for (auto db : faderTicksDb)
    {
        const auto y = (float) fader.getY() + (float) fader.getPositionOfValue (db);
        const auto isUnity = juce::exactlyEqual (db, 0.0f);
        g.setColour (isUnity ? Palette::textLabel.withAlpha (0.6f) : Palette::textAxis.withAlpha (0.5f));
        const auto len = isUnity ? 8.0f : 5.0f;
        g.fillRect (juce::Rectangle<float> (centreX - 26.0f - len, y - 0.5f, len, 1.0f));
        g.fillRect (juce::Rectangle<float> (centreX + 26.0f, y - 0.5f, len, 1.0f));
    }
}

void GainStrip::resized()
{
    auto area = getLocalBounds().reduced (10, 0);
    area.removeFromTop (66);
    area.removeFromBottom (14);

    // The fader's track (where values map) is inset by half a thumb at each end, above the readout
    const auto trackInset = ConverbLookAndFeel::faderThumbHeight / 2 + 2;
    const auto readoutHeight = 26 + 8;

    auto meterColumn = meterOnLeft ? area.removeFromLeft (40) : area.removeFromRight (40);
    area.removeFromLeft (meterOnLeft ? 4 : 0);
    area.removeFromRight (meterOnLeft ? 0 : 4);
    fader.setBounds (area);

    const auto meterBounds = (meterOnLeft ? meterColumn.removeFromRight (12) : meterColumn.removeFromLeft (12))
                                 .withTop (area.getY() + trackInset)
                                 .withBottom (area.getBottom() - readoutHeight - trackInset);
    meter.setBounds (meterBounds);
    scaleArea = meterColumn.withTrimmedLeft (meterOnLeft ? 0 : 4).withTrimmedRight (meterOnLeft ? 4 : 0);
}

//==============================================================================
RotaryControl::RotaryControl (const juce::String& titleIn, juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, const juce::String& minLabelIn, const juce::String& maxLabelIn)
    : title (titleIn), minLabel (minLabelIn), maxLabel (maxLabelIn), attachment (state, parameterID, knob)
{
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 28);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.setMouseDragSensitivity (220);
    configureSlider (knob, *state.getParameter (parameterID));
    knob.setTitle (title);
    addAndMakeVisible (knob);
}

void RotaryControl::paint (juce::Graphics& g)
{
    g.setColour (Palette::textLabel);
    g.setFont (ConverbLookAndFeel::titleFont (14.0f, 0.4f));
    g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (24), juce::Justification::centred);

    // End-of-travel labels beside the lower part of the knob
    g.setColour (Palette::textAxis);
    g.setFont (ConverbLookAndFeel::labelFont (11.5f));
    const auto radius = (float) std::min (knobArea.getWidth(), knobArea.getHeight()) * 0.5f;
    const auto labelY = (float) knobArea.getCentreY() + radius * 0.62f;
    const auto cx = (float) knobArea.getCentreX();
    g.drawText (minLabel, juce::Rectangle<float> (cx - radius - 58.0f, labelY, 54.0f, 16.0f), juce::Justification::centredRight);
    g.drawText (maxLabel, juce::Rectangle<float> (cx + radius + 4.0f, labelY, 54.0f, 16.0f), juce::Justification::centredLeft);
}

void RotaryControl::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (28);
    const auto size = std::min (area.getWidth(), area.getHeight() - 36);
    knob.setBounds (area.withSizeKeepingCentre (size, size + 36).withY (area.getY()));
    knobArea = knob.getBounds().withHeight (size);
}
