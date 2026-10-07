#pragma once

#include "dsp/ImpulseResponse.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Amplitude-vs-time plot of the loaded impulse response, and a drop target for audio files.
// Draws from precomputed peaks only; paths are rebuilt when the data, view or size changes.
class IRDisplay : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    enum class View
    {
        waveform, // linear amplitude, -1..+1
        envelope // peak envelope in dB
    };

    std::function<void (const juce::File&)> onFileDropped;
    std::function<bool (const juce::File&)> canLoadFile;

    void setData (std::shared_ptr<const ir::DisplayData> newData);
    void setView (View newView);
    View getView() const noexcept { return view; }

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void rebuildPaths();
    void drawGrid (juce::Graphics&) const;
    void drawCentredMessage (juce::Graphics&, const juce::String& text, juce::Colour colour, const juce::String& subText = {}) const;
    float amplitudeToY (float amplitude) const;
    float dbToY (float db) const;

    std::shared_ptr<const ir::DisplayData> data;
    View view = View::waveform;
    bool dragHover = false;

    juce::Rectangle<float> plot;
    double axisSeconds = 1.0, axisStep = 0.25;
    std::vector<juce::Path> fills, outlines; // per channel

    static constexpr float envelopeFloorDb = -60.0f;
};
