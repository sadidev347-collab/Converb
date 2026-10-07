#include "IRDisplay.h"
#include "ConverbLookAndFeel.h"
#include "Palette.h"

namespace
{
    // Picks a "nice" grid step giving at most ~8 divisions
    double niceStep (double range)
    {
        for (auto step : { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.25, 0.5, 1.0, 2.0 })
            if (range / step <= 8.0)
                return step;

        return 2.0;
    }

    juce::String formatTime (double seconds, double axisSeconds, double step, bool withUnit)
    {
        if (axisSeconds < 1.0)
            return juce::String (juce::roundToInt (seconds * 1000.0)) + (withUnit ? " ms" : "");

        // enough decimals to tell steps apart (0.25 s steps need two)
        const auto tenths = step * 10.0;
        const auto decimals = step >= 1.0 ? 1 : (std::abs (tenths - std::round (tenths)) < 1.0e-9 ? 1 : 2);
        return juce::String (seconds, decimals) + (withUnit ? " s" : "");
    }

    juce::Colour channelColour (size_t channel)
    {
        return channel == 0 ? Palette::accent : Palette::accentSoft;
    }
}

void IRDisplay::setData (std::shared_ptr<const ir::DisplayData> newData)
{
    data = std::move (newData);
    rebuildPaths();
    repaint();
}

void IRDisplay::setView (View newView)
{
    if (view == newView)
        return;

    view = newView;
    rebuildPaths();
    repaint();
}

void IRDisplay::resized()
{
    plot = getLocalBounds().toFloat().withTrimmedLeft (40.0f).withTrimmedRight (14.0f).withTrimmedTop (12.0f).withTrimmedBottom (24.0f);
    rebuildPaths();
}

float IRDisplay::amplitudeToY (float amplitude) const
{
    return plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, amplitude) * plot.getHeight() * 0.5f;
}

float IRDisplay::dbToY (float db) const
{
    return juce::jmap (juce::jlimit (envelopeFloorDb, 0.0f, db), envelopeFloorDb, 0.0f, plot.getBottom(), plot.getY());
}

void IRDisplay::rebuildPaths()
{
    fills.clear();
    outlines.clear();

    const auto hasPeaks = data != nullptr && ! data->peaks.empty() && data->lengthSeconds > 0.0;
    const auto length = hasPeaks ? data->lengthSeconds : 1.0;
    axisStep = niceStep (length);
    axisSeconds = std::ceil (length / axisStep - 1.0e-6) * axisStep;

    if (! hasPeaks || plot.isEmpty())
        return;

    const auto irWidth = (float) (plot.getWidth() * length / axisSeconds);
    const auto numColumns = std::max (1, (int) std::ceil (irWidth));

    for (const auto& bins : data->peaks)
    {
        const auto numBins = (int) bins.size();
        std::vector<juce::Range<float>> columns ((size_t) numColumns);

        for (int col = 0; col < numColumns; ++col)
        {
            const auto b0 = col * numBins / numColumns;
            const auto b1 = std::max (b0 + 1, (col + 1) * numBins / numColumns);
            auto range = bins[(size_t) b0];

            for (int b = b0 + 1; b < std::min (b1, numBins); ++b)
                range = range.getUnionWith (bins[(size_t) b]);

            columns[(size_t) col] = range;
        }

        const auto xOf = [&] (int col) { return plot.getX() + irWidth * (float) col / (float) std::max (1, numColumns - 1); };
        juce::Path fill, outline;

        if (view == View::waveform)
        {
            fill.startNewSubPath (xOf (0), amplitudeToY (columns[0].getEnd()));

            for (int col = 1; col < numColumns; ++col)
                fill.lineTo (xOf (col), amplitudeToY (columns[(size_t) col].getEnd()));

            for (int col = numColumns - 1; col >= 0; --col)
                fill.lineTo (xOf (col), amplitudeToY (columns[(size_t) col].getStart()));

            fill.closeSubPath();
            outline = fill;
        }
        else
        {
            const auto yOf = [&] (int col) {
                const auto& r = columns[(size_t) col];
                return dbToY (juce::Decibels::gainToDecibels (std::max (std::abs (r.getStart()), std::abs (r.getEnd())), envelopeFloorDb - 1.0f));
            };

            outline.startNewSubPath (xOf (0), yOf (0));

            for (int col = 1; col < numColumns; ++col)
                outline.lineTo (xOf (col), yOf (col));

            fill = outline;
            fill.lineTo (xOf (numColumns - 1), plot.getBottom());
            fill.lineTo (xOf (0), plot.getBottom());
            fill.closeSubPath();
        }

        fills.push_back (std::move (fill));
        outlines.push_back (std::move (outline));
    }
}

void IRDisplay::drawGrid (juce::Graphics& g) const
{
    g.setFont (ConverbLookAndFeel::valueFont (10.5f));

    // Time axis
    const auto numSteps = juce::roundToInt (axisSeconds / axisStep);

    for (int i = 0; i <= numSteps; ++i)
    {
        const auto t = axisStep * i;
        const auto x = plot.getX() + plot.getWidth() * (float) (t / axisSeconds);

        g.setColour (Palette::grid);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
        g.setColour (Palette::textAxis);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, plot.getBottom(), 1.0f, 4.0f));

        const auto label = formatTime (t, axisSeconds, axisStep, i == numSteps);
        const auto justification = i == numSteps ? juce::Justification::centredRight : (i == 0 ? juce::Justification::centredLeft : juce::Justification::centred);
        auto labelArea = juce::Rectangle<float> (60.0f, 14.0f).withCentre ({ x, plot.getBottom() + 14.0f });

        if (i == numSteps)
            labelArea = labelArea.withRightX (x + 6.0f);
        else if (i == 0)
            labelArea = labelArea.withX (x - 6.0f);

        g.drawText (label, labelArea, justification);
    }

    // Amplitude axis
    const auto drawRow = [&] (float y, const juce::String& label, bool strong) {
        g.setColour (strong ? Palette::textAxis.withAlpha (0.5f) : Palette::grid);
        g.fillRect (juce::Rectangle<float> (plot.getX(), y - 0.5f, plot.getWidth(), 1.0f));
        g.setColour (Palette::textAxis);
        g.drawText (label, juce::Rectangle<float> (0.0f, y - 7.0f, plot.getX() - 6.0f, 14.0f), juce::Justification::centredRight);
    };

    if (view == View::waveform)
    {
        const std::array<std::pair<float, const char*>, 5> rows { { { 1.0f, "1" }, { 0.5f, "0.5" }, { 0.0f, "0" }, { -0.5f, "-0.5" }, { -1.0f, "-1" } } };

        for (const auto& [amplitude, label] : rows)
            drawRow (amplitudeToY (amplitude), label, juce::exactlyEqual (amplitude, 0.0f));
    }
    else
    {
        for (int db = 0; db >= (int) envelopeFloorDb; db -= 12)
            drawRow (dbToY ((float) db), juce::String (db) + (db == 0 ? " dB" : ""), false);
    }

    g.setColour (Palette::panelBorder);
    g.drawRect (plot, 1.0f);
}

void IRDisplay::drawCentredMessage (juce::Graphics& g, const juce::String& text, juce::Colour colour, const juce::String& subText) const
{
    auto area = plot.toNearestInt();
    g.setColour (colour);
    g.setFont (ConverbLookAndFeel::labelFont (15.0f));
    g.drawFittedText (text, area.withTrimmedBottom (subText.isEmpty() ? 0 : 24), juce::Justification::centred, 2);

    if (subText.isNotEmpty())
    {
        g.setColour (Palette::textAxis);
        g.setFont (ConverbLookAndFeel::labelFont (12.0f));
        g.drawFittedText (subText, area.withTrimmedTop (28), juce::Justification::centred, 2);
    }
}

void IRDisplay::paint (juce::Graphics& g)
{
    using Status = ir::DisplayData::Status;

    g.setColour (Palette::panelInset.withAlpha (0.6f));
    g.fillRect (getLocalBounds());

    drawGrid (g);

    // Waveform(s): second channel underneath, first on top
    for (auto i = fills.size(); i-- > 0;)
    {
        const auto colour = channelColour (i);
        const auto alphaScale = i == 0 ? 1.0f : 0.6f;

        if (view == View::waveform)
        {
            juce::ColourGradient gradient (colour.withAlpha (0.08f * alphaScale), 0.0f, plot.getY(), colour.withAlpha (0.08f * alphaScale), 0.0f, plot.getBottom(), false);
            gradient.addColour (0.5, colour.withAlpha (0.6f * alphaScale));
            g.setGradientFill (gradient);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f * alphaScale), 0.0f, plot.getY(), colour.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        }

        g.fillPath (fills[i]);

        g.setColour (colour.withAlpha (0.18f * alphaScale));
        g.strokePath (outlines[i], juce::PathStrokeType (3.0f, juce::PathStrokeType::curved));
        g.setColour (colour.withAlpha (0.95f * alphaScale));
        g.strokePath (outlines[i], juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));
    }

    const auto hasWaveform = ! fills.empty();

    if (hasWaveform)
    {
        // Length badge
        const auto badge = juce::Rectangle<float> (70.0f, 24.0f).withPosition (plot.getRight() - 80.0f, plot.getY() + 10.0f);
        g.setColour (Palette::panelInset.withAlpha (0.9f));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (Palette::panelBorder);
        g.drawRoundedRectangle (badge, 4.0f, 1.0f);
        g.setColour (Palette::textLabel);
        g.setFont (ConverbLookAndFeel::valueFont (13.0f));
        g.drawText (juce::String (data->lengthSeconds, 2) + " s", badge, juce::Justification::centred);

        if (fills.size() > 1)
        {
            g.setFont (ConverbLookAndFeel::labelFont (11.0f));
            auto legend = juce::Rectangle<float> (badge.getX() - 70.0f, badge.getCentreY() - 7.0f, 14.0f, 14.0f);

            for (size_t ch = 0; ch < 2; ++ch)
            {
                g.setColour (channelColour (ch));
                g.fillRect (legend.withWidth (8.0f).withSizeKeepingCentre (8.0f, 2.0f));
                g.drawText (ch == 0 ? "L" : "R", legend.withTrimmedLeft (11.0f).withWidth (14.0f), juce::Justification::centredLeft);
                legend.translate (30.0f, 0.0f);
            }
        }
    }

    const auto status = data != nullptr ? data->status : Status::empty;

    if (status == Status::missing)
        drawCentredMessage (g, "IR missing: " + data->name, Palette::error, "The file and embedded copy are unavailable. Load it again to restore the reverb.");
    else if (status == Status::loading && ! hasWaveform)
        drawCentredMessage (g, data->message, Palette::textLabel);
    else if (! hasWaveform && status != Status::error)
        drawCentredMessage (g, "Drop an impulse response here or click Load IR", Palette::textLabel, "WAV, AIFF or FLAC, up to 10 s");

    if (status == Status::error || (status == Status::loading && hasWaveform))
    {
        // Banner along the bottom of the plot, keeping the current waveform visible
        const auto banner = plot.withTop (plot.getBottom() - 30.0f).reduced (10.0f, 4.0f);
        g.setColour (Palette::panelInset.withAlpha (0.92f));
        g.fillRoundedRectangle (banner, 4.0f);
        g.setColour (status == Status::error ? Palette::error : Palette::panelBorder);
        g.drawRoundedRectangle (banner, 4.0f, 1.0f);
        g.setFont (ConverbLookAndFeel::labelFont (12.0f));
        g.drawFittedText (data->message, banner.toNearestInt().reduced (8, 0), juce::Justification::centred, 1);
    }

    if (dragHover)
    {
        g.setColour (Palette::accent.withAlpha (0.08f));
        g.fillRect (getLocalBounds());
        g.setColour (Palette::accent);
        g.drawRect (getLocalBounds().toFloat().reduced (1.0f), 2.0f);
        g.setFont (ConverbLookAndFeel::titleFont (16.0f, 0.2f));
        g.drawText ("DROP TO LOAD", getLocalBounds(), juce::Justification::centred);
    }
}

//==============================================================================
bool IRDisplay::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && canLoadFile != nullptr && canLoadFile (juce::File (files[0]));
}

void IRDisplay::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHover = true;
    repaint();
}

void IRDisplay::fileDragExit (const juce::StringArray&)
{
    dragHover = false;
    repaint();
}

void IRDisplay::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();

    if (onFileDropped != nullptr && ! files.isEmpty())
        onFileDropped (juce::File (files[0]));
}
