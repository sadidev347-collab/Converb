#pragma once

#include "Controls.h"
#include "IRPanel.h"

class PluginProcessor;

// The whole UI at its design size; the editor scales it to the window.
class MainView : public juce::Component, private juce::Timer
{
public:
    static constexpr int designWidth = 900;
    static constexpr int designHeight = 600;

    explicit MainView (PluginProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshIRData();
    void setView (IRDisplay::View view);

    PluginProcessor& processor;
    GainStrip inputStrip, outputStrip;
    IRPanel irPanel;
    RotaryControl amountKnob, mixKnob;
    juce::TextButton waveformViewButton { "WAVEFORM" }, envelopeViewButton { "dB ENVELOPE" };
    juce::TooltipWindow tooltips { this, 600 };

    std::shared_ptr<const ir::DisplayData> irData;
    int irVersion = -1;
    juce::Image noise;
    juce::Rectangle<int> headerArea, controlsArea, footerArea;
};
