#include "IRPanel.h"
#include "ConverbLookAndFeel.h"
#include "Palette.h"
#include "PluginProcessor.h"

namespace
{
    juce::String formatSampleRate (double sampleRate)
    {
        const auto khz = sampleRate / 1000.0;
        const auto isWhole = std::abs (khz - std::round (khz)) < 0.05;
        return juce::String (khz, isWhole ? 0 : 1) + " kHz";
    }

    juce::String formatChannels (int numChannels)
    {
        return numChannels == 1 ? "Mono" : (numChannels == 2 ? "Stereo" : juce::String (numChannels) + " ch");
    }

    juce::String formatDb (float db)
    {
        return db <= -99.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
    }
}

void IRPanel::LoadButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    getLookAndFeel().drawButtonBackground (g, *this, {}, isMouseOver, isButtonDown);

    const auto active = isMouseOver || isButtonDown;
    const auto colour = active ? Palette::accent : Palette::textLabel;
    auto area = getLocalBounds().toFloat().reduced (12.0f, 0.0f);

    // Folder icon
    const auto icon = area.removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, 14.0f);
    juce::Path folder;
    folder.startNewSubPath (icon.getX(), icon.getBottom());
    folder.lineTo (icon.getX(), icon.getY());
    folder.lineTo (icon.getX() + 6.0f, icon.getY());
    folder.lineTo (icon.getX() + 8.0f, icon.getY() + 2.5f);
    folder.lineTo (icon.getRight(), icon.getY() + 2.5f);
    folder.lineTo (icon.getRight(), icon.getBottom());
    folder.closeSubPath();
    g.setColour (colour);
    g.strokePath (folder, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont (ConverbLookAndFeel::labelFont (14.0f));
    g.drawText (getButtonText(), area.withTrimmedLeft (10.0f), juce::Justification::centredLeft);
}

//==============================================================================
IRPanel::IRPanel (PluginProcessor& p) : processor (p)
{
    loadButton.setTooltip ("Load an impulse response (or drop one on the graph)");
    loadButton.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton);

    display.canLoadFile = [this] (const juce::File& f) { return canLoad (f); };
    display.onFileDropped = [this] (const juce::File& f) { processor.loadImpulseResponse (f); };
    addAndMakeVisible (display);
}

bool IRPanel::canLoad (const juce::File& file) const
{
    return processor.getFormatManager().findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

void IRPanel::openFileChooser()
{
    auto startDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    if (data != nullptr && data->path.isNotEmpty())
        startDir = juce::File (data->path).getParentDirectory();

    chooser = std::make_unique<juce::FileChooser> ("Load impulse response", startDir, processor.getFormatManager().getWildcardForAllFormats());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<IRPanel> (this)] (const juce::FileChooser& fc) {
            if (safeThis == nullptr)
                return;

            const auto file = fc.getResult();

            if (file.existsAsFile())
                safeThis->processor.loadImpulseResponse (file);
        });
}

void IRPanel::setData (std::shared_ptr<const ir::DisplayData> newData)
{
    data = std::move (newData);
    display.setData (data);
    repaint();
}

void IRPanel::paint (juce::Graphics& g)
{
    ConverbLookAndFeel::drawPanel (g, getLocalBounds().toFloat());

    const auto hasIR = data != nullptr && ! data->peaks.empty();

    // Header: file name and summary
    auto header = headerArea.withTrimmedLeft (loadButton.getRight() + 16);
    auto infoArea = header.removeFromRight (220);

    g.setFont (ConverbLookAndFeel::valueFont (17.0f));
    if (data != nullptr && data->status == ir::DisplayData::Status::missing)
    {
        g.setColour (Palette::error);
        g.drawFittedText (data->name + " (missing)", header, juce::Justification::centredLeft, 1);
    }
    else
    {
        g.setColour (hasIR ? Palette::textValue : Palette::textAxis);
        g.drawFittedText (hasIR ? data->name : juce::String ("No impulse response"), header, juce::Justification::centredLeft, 1);
    }

    if (hasIR)
    {
        g.setColour (Palette::textAxis);
        g.setFont (ConverbLookAndFeel::valueFont (13.0f));
        const auto info = juce::String (data->lengthSeconds, 1) + " s   |   " + formatSampleRate (data->sampleRate) + "   |   " + formatChannels (data->numChannels);
        g.drawFittedText (info, infoArea.withTrimmedRight (16), juce::Justification::centredRight, 1);
    }

    g.setColour (Palette::panelBorder);
    g.fillRect (headerArea.getX(), headerArea.getBottom(), headerArea.getWidth(), 1);
    g.fillRect (statsArea.getX(), statsArea.getY(), statsArea.getWidth(), 1);

    // Stats row
    const std::array<std::pair<const char*, juce::String>, 5> stats { {
        { "RMS", hasIR ? formatDb (data->rmsDb) : "-" },
        { "PEAK", hasIR ? formatDb (data->peakDb) : "-" },
        { "LENGTH", hasIR ? juce::String (data->lengthSeconds, 2) + " s" + (data->wasTruncated ? " (capped)" : "") : "-" },
        { "CHANNELS", hasIR ? formatChannels (data->numChannels) : "-" },
        { "SAMPLE RATE", hasIR ? formatSampleRate (data->sampleRate) : "-" },
    } };

    auto row = statsArea.reduced (24, 10);
    const auto columnWidth = row.getWidth() / (int) stats.size();

    for (size_t i = 0; i < stats.size(); ++i)
    {
        auto column = row.removeFromLeft (columnWidth);

        if (i == 3 || i == 4)
        {
            g.setColour (Palette::panelBorder);
            g.fillRect (column.getX() - 14, column.getY() + 4, 1, column.getHeight() - 8);
        }

        g.setColour (Palette::textAxis);
        g.setFont (ConverbLookAndFeel::titleFont (10.5f, 0.15f));
        g.drawText (stats[i].first, column.removeFromTop (column.getHeight() / 2), juce::Justification::bottomLeft);
        g.setColour (Palette::textValue);
        g.setFont (ConverbLookAndFeel::valueFont (15.0f));
        g.drawText (stats[i].second, column, juce::Justification::centredLeft);
    }
}

void IRPanel::resized()
{
    auto area = getLocalBounds();
    headerArea = area.removeFromTop (56);
    statsArea = area.removeFromBottom (60);

    loadButton.setBounds (headerArea.reduced (14, 11).withWidth (120));
    display.setBounds (area.reduced (1, 0));
}
