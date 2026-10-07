#include "MainView.h"
#include "ConverbLookAndFeel.h"
#include "Palette.h"
#include "PluginProcessor.h"

namespace
{
    constexpr auto viewProperty = "irView";

    // Faint monochrome grain, tiled over the background gradient to hide banding
    juce::Image makeNoiseImage()
    {
        juce::Image image (juce::Image::ARGB, 128, 128, true);
        juce::Random random (0x5eed);

        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                image.setPixelAt (x, y, juce::Colour::greyLevel (random.nextBool() ? 1.0f : 0.0f).withAlpha (random.nextFloat() * 0.035f));

        return image;
    }
}

MainView::MainView (PluginProcessor& p)
    : processor (p),
      inputStrip ("INPUT", p.getValueTreeState(), ParamIDs::inputGain, true),
      outputStrip ("OUTPUT", p.getValueTreeState(), ParamIDs::outputGain, false),
      irPanel (p),
      amountKnob ("Reverb Amount", p.getValueTreeState(), ParamIDs::reverbAmount, juce::String::fromUTF8 ("-\xe2\x88\x9e"), "0 dB"),
      mixKnob ("Mix", p.getValueTreeState(), ParamIDs::mix, "DRY", "WET"),
      noise (makeNoiseImage())
{
    setOpaque (true);

    for (auto* c : std::initializer_list<juce::Component*> { &inputStrip, &outputStrip, &irPanel, &amountKnob, &mixKnob, &waveformViewButton, &envelopeViewButton })
        addAndMakeVisible (c);

    for (auto* b : { &waveformViewButton, &envelopeViewButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
    }

    waveformViewButton.setTooltip ("Linear amplitude");
    envelopeViewButton.setTooltip ("Peak envelope in dB");
    waveformViewButton.onClick = [this] { if (waveformViewButton.getToggleState()) setView (IRDisplay::View::waveform); };
    envelopeViewButton.onClick = [this] { if (envelopeViewButton.getToggleState()) setView (IRDisplay::View::envelope); };

    const auto savedView = (int) processor.getValueTreeState().state.getProperty (viewProperty, 0);
    setView (savedView == 1 ? IRDisplay::View::envelope : IRDisplay::View::waveform);

    setSize (designWidth, designHeight);
    refreshIRData();
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
}

void MainView::setView (IRDisplay::View view)
{
    irPanel.getDisplay().setView (view);
    const auto isEnvelope = view == IRDisplay::View::envelope;
    waveformViewButton.setToggleState (! isEnvelope, juce::dontSendNotification);
    envelopeViewButton.setToggleState (isEnvelope, juce::dontSendNotification);
    processor.getValueTreeState().state.setProperty (viewProperty, isEnvelope ? 1 : 0, nullptr);
}

void MainView::timerCallback()
{
    inputStrip.setPeaks (processor.takeInputPeaks());
    outputStrip.setPeaks (processor.takeOutputPeaks());

    if (processor.getIRDisplayVersion() != irVersion)
        refreshIRData();
}

void MainView::refreshIRData()
{
    irVersion = processor.getIRDisplayVersion();
    irData = processor.getIRDisplayData();
    irPanel.setData (irData);
    repaint (footerArea);
}

void MainView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Background: multi-stop black -> charcoal gradient plus grain
    juce::ColourGradient gradient (Palette::backgroundTop, 0.0f, 0.0f, Palette::backgroundBottom, 0.0f, bounds.getBottom(), false);
    gradient.addColour (0.35, Palette::backgroundMid);
    gradient.addColour (0.7, Palette::backgroundTop.interpolatedWith (Palette::backgroundBottom, 0.6f));
    g.setGradientFill (gradient);
    g.fillAll();

    g.setTiledImageFill (noise, 0, 0, 1.0f);
    g.fillAll();

    // Header
    g.setColour (Palette::textValue);
    g.setFont (ConverbLookAndFeel::titleFont (22.0f, 0.6f));
    g.drawText ("CONVERB", headerArea.withTrimmedLeft (28).withHeight (36).withY (headerArea.getY() + 8), juce::Justification::bottomLeft);
    g.setColour (Palette::textAxis);
    g.setFont (ConverbLookAndFeel::titleFont (10.5f, 0.45f));
    g.drawText ("CONVOLUTION REVERB", headerArea.withTrimmedLeft (29).withTrimmedTop (44).withHeight (14), juce::Justification::centredLeft);

    g.setColour (Palette::accent);
    g.fillRect (juce::Rectangle<float> (28.0f, (float) headerArea.getBottom() - 4.0f, 28.0f, 2.0f));

    g.setColour (Palette::textAxis);
    g.setFont (ConverbLookAndFeel::labelFont (11.0f));
    g.drawText (juce::String::fromUTF8 ("ADIDEV PLUGIN SUITE  \xc2\xb7  v" VERSION), headerArea.withTrimmedRight (28), juce::Justification::centredRight);

    // Controls panel with a divider between the two knobs
    ConverbLookAndFeel::drawPanel (g, controlsArea.toFloat());
    g.setColour (Palette::panelBorder);
    g.fillRect (controlsArea.getCentreX(), controlsArea.getY() + 22, 1, controlsArea.getHeight() - 44);

    // Footer: IR status and path
    ConverbLookAndFeel::drawPanel (g, footerArea.toFloat());

    using Status = ir::DisplayData::Status;
    const auto status = irData != nullptr ? irData->status : Status::empty;
    const auto hasIR = irData != nullptr && ! irData->peaks.empty();
    const auto statusColour = status == Status::error || status == Status::missing ? Palette::error
                              : hasIR                                              ? Palette::accent
                                                                                   : Palette::idle;

    auto footer = footerArea.reduced (10, 6);
    auto pill = footer.removeFromLeft (64).toFloat();
    g.setColour (Palette::panelInset);
    g.fillRoundedRectangle (pill, 5.0f);
    g.setColour (Palette::panelBorder);
    g.drawRoundedRectangle (pill.reduced (0.5f), 5.0f, 1.0f);

    const auto dot = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ pill.getX() + 18.0f, pill.getCentreY() });
    g.setColour (statusColour.withAlpha (0.3f));
    g.fillEllipse (dot.expanded (3.0f));
    g.setColour (statusColour);
    g.fillEllipse (dot);
    g.setColour (Palette::textLabel);
    g.setFont (ConverbLookAndFeel::labelFont (12.0f));
    g.drawText ("IR", pill.withTrimmedLeft (30.0f), juce::Justification::centredLeft);

    footer.removeFromLeft (14);
    g.setColour (Palette::panelBorder);
    g.fillRect (footer.getX(), footer.getY() + 4, 1, footer.getHeight() - 8);
    footer.removeFromLeft (14);

    juce::String statusText = "No impulse response loaded";

    if (irData != nullptr)
    {
        if (status == Status::loading || status == Status::error)
            statusText = irData->message;
        else if (status == Status::missing)
            statusText = "IR missing: " + (irData->path.isNotEmpty() ? irData->path : irData->name);
        else if (hasIR)
            statusText = irData->path.isNotEmpty() ? irData->path : irData->name + " (embedded)";
    }

    g.setColour (status == Status::error || status == Status::missing ? Palette::error : Palette::textLabel);
    g.setFont (ConverbLookAndFeel::valueFont (13.0f));
    g.drawFittedText (statusText, footer.withTrimmedRight (240), juce::Justification::centredLeft, 1);
}

void MainView::resized()
{
    auto area = getLocalBounds();
    headerArea = area.removeFromTop (64);
    area.reduce (12, 0);

    footerArea = area.removeFromBottom (44).withTrimmedBottom (8);
    area.removeFromBottom (8);

    inputStrip.setBounds (area.removeFromLeft (128));
    outputStrip.setBounds (area.removeFromRight (128));
    area.reduce (8, 0);

    controlsArea = area.removeFromBottom (176);
    area.removeFromBottom (8);
    irPanel.setBounds (area);

    auto controls = controlsArea.reduced (20, 14);
    amountKnob.setBounds (controls.removeFromLeft (controls.getWidth() / 2).reduced (40, 0));
    mixKnob.setBounds (controls.reduced (40, 0));

    auto viewButtons = footerArea.reduced (8, 6).removeFromRight (220);
    envelopeViewButton.setBounds (viewButtons.removeFromRight (112));
    viewButtons.removeFromRight (6);
    waveformViewButton.setBounds (viewButtons);
}
