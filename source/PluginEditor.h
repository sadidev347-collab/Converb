#pragma once

#include "PluginProcessor.h"
#include "ui/ConverbLookAndFeel.h"
#include "ui/MainView.h"

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PluginProcessor& processorRef;
    ConverbLookAndFeel lookAndFeel; // must outlive the components using it
    MainView view;
    bool rememberSize = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
