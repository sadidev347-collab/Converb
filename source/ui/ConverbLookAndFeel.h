#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ConverbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ConverbLookAndFeel();

    // Letter-spaced caps used for section titles
    static juce::Font titleFont (float height, float tracking = 0.25f);
    static juce::Font labelFont (float height);
    static juce::Font valueFont (float height);

    static void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds);

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    static constexpr int faderThumbHeight = 34;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConverbLookAndFeel)
};
