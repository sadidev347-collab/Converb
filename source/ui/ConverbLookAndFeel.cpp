#include "ConverbLookAndFeel.h"
#include "Palette.h"

namespace
{
    // Slider value readout: white text in a rounded inset box
    class ValueBox : public juce::Label
    {
    public:
        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (Palette::panelInset);
            g.fillRoundedRectangle (bounds, 5.0f);
            g.setColour (isBeingEdited() ? Palette::accent : Palette::panelBorder);
            g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

            if (! isBeingEdited())
            {
                g.setColour (Palette::textValue);
                g.setFont (getFont());
                g.drawFittedText (getText(), getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
            }
        }
    };

    void strokeWithGlow (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width)
    {
        const juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::rounded;
        g.setColour (colour.withAlpha (0.12f));
        g.strokePath (path, juce::PathStrokeType (width * 3.2f, juce::PathStrokeType::curved, cap));
        g.setColour (colour.withAlpha (0.25f));
        g.strokePath (path, juce::PathStrokeType (width * 1.9f, juce::PathStrokeType::curved, cap));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, cap));
    }
}

ConverbLookAndFeel::ConverbLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::backgroundTop);
    setColour (juce::Slider::textBoxTextColourId, Palette::textValue);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, Palette::accent.withAlpha (0.35f));
    setColour (juce::Label::textColourId, Palette::textLabel);
    setColour (juce::Label::textWhenEditingColourId, Palette::textValue);
    setColour (juce::TextEditor::backgroundColourId, Palette::panelInset);
    setColour (juce::TextEditor::textColourId, Palette::textValue);
    setColour (juce::TextEditor::highlightColourId, Palette::accent.withAlpha (0.35f));
    setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
    setColour (juce::CaretComponent::caretColourId, Palette::accent);
    setColour (juce::TextButton::buttonColourId, Palette::panelInset);
    setColour (juce::TextButton::textColourOffId, Palette::textLabel);
    setColour (juce::TextButton::textColourOnId, Palette::accent);
    setColour (juce::TooltipWindow::backgroundColourId, Palette::panelInset);
    setColour (juce::TooltipWindow::textColourId, Palette::textLabel);
    setColour (juce::TooltipWindow::outlineColourId, Palette::panelBorder);
}

juce::Font ConverbLookAndFeel::titleFont (float height, float tracking)
{
    return juce::Font (juce::FontOptions (height).withKerningFactor (tracking));
}

juce::Font ConverbLookAndFeel::labelFont (float height)
{
    return juce::Font (juce::FontOptions (height).withKerningFactor (0.08f));
}

juce::Font ConverbLookAndFeel::valueFont (float height)
{
    return juce::Font (juce::FontOptions (height));
}

void ConverbLookAndFeel::drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, Palette::panelCornerRadius);
    g.setColour (Palette::panelBorder);
    g.drawRoundedRectangle (bounds.reduced (0.5f), Palette::panelCornerRadius, 1.0f);
}

//==============================================================================
void ConverbLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f - 6.0f;
    const auto centre = bounds.getCentre();
    const auto arcWidth = std::max (3.0f, radius * 0.07f);
    const auto arcRadius = radius - arcWidth;
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Recessed ring the value arc sits in
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f + 4.0f, radius * 2.0f + 4.0f).withCentre (centre));

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (Palette::track);
    g.strokePath (track, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);
        strokeWithGlow (g, value, slider.isEnabled() ? Palette::accent : Palette::idle, arcWidth);
    }

    // Knob body
    const auto knobRadius = arcRadius - arcWidth * 2.2f;
    const auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (knob.translated (0.0f, knobRadius * 0.06f).expanded (2.0f));

    g.setGradientFill (juce::ColourGradient (Palette::knobHighlight, knob.getX(), knob.getY(), Palette::knobBody, knob.getRight(), knob.getBottom(), false));
    g.fillEllipse (knob);

    const auto face = knob.reduced (knobRadius * 0.08f);
    g.setGradientFill (juce::ColourGradient (Palette::knobBody.brighter (0.08f), face.getCentreX(), face.getY(), Palette::knobBody.darker (0.4f), face.getCentreX(), face.getBottom(), false));
    g.fillEllipse (face);

    g.setColour (Palette::panelBorder);
    g.drawEllipse (knob, 1.0f);

    // Pointer
    const auto direction = juce::Point<float> (std::sin (angle), -std::cos (angle));
    juce::Path pointer;
    pointer.startNewSubPath (centre + direction * (knobRadius * 0.45f));
    pointer.lineTo (centre + direction * (knobRadius * 0.82f));
    g.setColour (Palette::textValue);
    g.strokePath (pointer, juce::PathStrokeType (std::max (2.0f, knobRadius * 0.05f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ConverbLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto centreX = bounds.getCentreX();

    // Slot
    const auto slot = juce::Rectangle<float> (6.0f, bounds.getHeight()).withCentre ({ centreX, bounds.getCentreY() });
    g.setColour (Palette::panelInset);
    g.fillRoundedRectangle (slot, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (slot, 3.0f, 1.0f);

    // Fill from the bottom up to the thumb
    juce::Path fill;
    fill.startNewSubPath (centreX, bounds.getBottom());
    fill.lineTo (centreX, sliderPos);
    strokeWithGlow (g, fill, slider.isEnabled() ? Palette::accent : Palette::idle, 2.5f);

    // Thumb
    const auto thumbWidth = std::min (bounds.getWidth() - 4.0f, 44.0f);
    const auto thumb = juce::Rectangle<float> (thumbWidth, (float) faderThumbHeight).withCentre ({ centreX, sliderPos });

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (thumb.translated (0.0f, 3.0f), 4.0f);

    g.setGradientFill (juce::ColourGradient (Palette::thumbTop, thumb.getX(), thumb.getY(), Palette::thumbBottom, thumb.getX(), thumb.getBottom(), false));
    g.fillRoundedRectangle (thumb, 4.0f);

    // Ridges above and below the centre line
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (auto offset : { -9.0f, -6.0f, 6.0f, 9.0f })
        g.fillRect (juce::Rectangle<float> (thumb.getWidth() - 14.0f, 1.0f).withCentre ({ centreX, sliderPos + offset }));

    g.setColour (slider.isMouseOverOrDragging() ? Palette::accent.withAlpha (0.8f) : Palette::panelBorder.brighter (0.2f));
    g.drawRoundedRectangle (thumb.reduced (0.5f), 4.0f, 1.0f);

    juce::Path centreLine;
    centreLine.startNewSubPath (thumb.getX() + 4.0f, sliderPos);
    centreLine.lineTo (thumb.getRight() - 4.0f, sliderPos);
    strokeWithGlow (g, centreLine, slider.isEnabled() ? Palette::accent : Palette::idle, 2.0f);
}

juce::Label* ConverbLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = new ValueBox();
    label->setJustificationType (juce::Justification::centred);
    label->setFont (valueFont (slider.isRotary() ? 15.0f : 14.0f));
    label->setColour (juce::Label::textColourId, Palette::textValue);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::backgroundColourId, Palette::panelInset);
    label->setColour (juce::TextEditor::textColourId, Palette::textValue);
    label->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::highlightColourId, Palette::accent.withAlpha (0.35f));
    label->setMinimumHorizontalScale (1.0f);
    return label;
}

juce::Slider::SliderLayout ConverbLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    juce::Slider::SliderLayout layout;
    auto bounds = slider.getLocalBounds();

    if (slider.getTextBoxPosition() == juce::Slider::TextBoxBelow)
    {
        const auto boxHeight = slider.getTextBoxHeight();
        layout.textBoxBounds = bounds.removeFromBottom (boxHeight).withSizeKeepingCentre (std::min (slider.getTextBoxWidth(), bounds.getWidth()), boxHeight);
        bounds.removeFromBottom (8);
    }

    if (slider.getSliderStyle() == juce::Slider::LinearVertical)
        bounds.reduce (0, faderThumbHeight / 2 + 2);

    layout.sliderBounds = bounds;
    return layout;
}

//==============================================================================
void ConverbLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool isMouseOverButton, bool isButtonDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto active = button.getToggleState() || isButtonDown;

    g.setColour (active ? Palette::accent.withAlpha (0.14f) : Palette::panelInset);
    g.fillRoundedRectangle (bounds, 6.0f);

    g.setColour (active || isMouseOverButton ? Palette::accent.withAlpha (active ? 0.9f : 0.6f) : Palette::panelBorder);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
}

void ConverbLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton, bool isButtonDown)
{
    const auto active = button.getToggleState() || isButtonDown || isMouseOverButton;
    g.setColour (active ? Palette::accent : Palette::textLabel);
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
}

juce::Font ConverbLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return labelFont (std::min (13.0f, (float) buttonHeight * 0.5f));
}
