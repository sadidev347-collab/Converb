#pragma once

#include "IRDisplay.h"

class PluginProcessor;

// Centre panel: Load IR button and file info, the IR plot, and a row of IR statistics.
class IRPanel : public juce::Component
{
public:
    explicit IRPanel (PluginProcessor&);

    void setData (std::shared_ptr<const ir::DisplayData> newData);
    IRDisplay& getDisplay() noexcept { return display; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class LoadButton : public juce::Button
    {
    public:
        LoadButton() : juce::Button ("Load IR") {}
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
    };

    void openFileChooser();
    bool canLoad (const juce::File&) const;

    PluginProcessor& processor;
    LoadButton loadButton;
    IRDisplay display;
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<const ir::DisplayData> data;
    juce::Rectangle<int> headerArea, statsArea;
};
