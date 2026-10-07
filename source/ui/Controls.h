#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Vertical stereo peak meter. Fed from a timer on the message thread via setPeaks().
class LevelMeter : public juce::Component
{
public:
    static constexpr float minDb = -60.0f;
    static constexpr float maxDb = 6.0f;

    void setPeaks (std::array<float, 2> linearPeaks);
    float dbToY (float db) const; // in this component's coordinates

    void paint (juce::Graphics&) override;

private:
    std::array<float, 2> levelsDb { minDb, minDb };
    std::array<float, 2> holdDb { minDb, minDb };
    std::array<int, 2> holdFrames { 0, 0 };
};

// Shared slider behaviour: double-click resets, shift-drag for fine control.
void configureSlider (juce::Slider& slider, juce::RangedAudioParameter& parameter);

// Titled panel with a level meter, a gain fader and a dB readout.
class GainStrip : public juce::Component
{
public:
    GainStrip (const juce::String& title, juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, bool meterOnLeft);

    void setPeaks (std::array<float, 2> linearPeaks) { meter.setPeaks (linearPeaks); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title;
    bool meterOnLeft;
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::TextBoxBelow };
    LevelMeter meter;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::Rectangle<int> scaleArea;
};

// Titled rotary knob with a value readout and labels at both ends of its travel.
class RotaryControl : public juce::Component
{
public:
    RotaryControl (const juce::String& title, juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, const juce::String& minLabel, const juce::String& maxLabel);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title, minLabel, maxLabel;
    juce::Slider knob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::Rectangle<int> knobArea;
};
