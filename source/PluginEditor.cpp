#include "PluginEditor.h"

namespace
{
    constexpr auto sizeProperty = "editorWidth";
    constexpr double aspectRatio = (double) MainView::designWidth / MainView::designHeight;
}

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), view (p)
{
    setLookAndFeel (&lookAndFeel);
    view.setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);

    // Resizable with a fixed aspect ratio; the view is laid out once at design size and scaled
    setResizable (true, true);
    setResizeLimits (MainView::designWidth * 4 / 5, MainView::designHeight * 4 / 5, MainView::designWidth * 2, MainView::designHeight * 2);
    getConstrainer()->setFixedAspectRatio (aspectRatio);

    const auto savedWidth = (int) processorRef.getValueTreeState().state.getProperty (sizeProperty, MainView::designWidth);
    const auto width = juce::jlimit (MainView::designWidth * 4 / 5, MainView::designWidth * 2, savedWidth);
    setSize (width, juce::roundToInt (width / aspectRatio));

    // Only user/host resizes from here on (the limits above already resized us to the minimum)
    rememberSize = true;
}

PluginEditor::~PluginEditor()
{
    view.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void PluginEditor::resized()
{
    const auto scale = (float) getWidth() / (float) MainView::designWidth;
    view.setBounds (0, 0, MainView::designWidth, MainView::designHeight);
    view.setTransform (juce::AffineTransform::scale (scale));

    if (rememberSize)
        processorRef.getValueTreeState().state.setProperty (sizeProperty, getWidth(), nullptr);
}
