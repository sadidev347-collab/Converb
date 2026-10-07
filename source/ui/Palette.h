#pragma once

#include <juce_graphics/juce_graphics.h>

// Black, gray and yellow. All UI colours come from here.
namespace Palette
{
    inline const juce::Colour backgroundTop { 0xff0a0a0a };
    inline const juce::Colour backgroundMid { 0xff151515 };
    inline const juce::Colour backgroundBottom { 0xff2a2a2a };

    inline const juce::Colour panel { 0xcc1a1a1a }; // #1A1A1A at ~80 %
    inline const juce::Colour panelInset { 0xff121212 };
    inline const juce::Colour panelBorder { 0xff3a3a3a };
    inline const juce::Colour track { 0xff2a2a2a };

    inline const juce::Colour accent { 0xffffc400 };
    inline const juce::Colour accentSoft { 0xffffe17a }; // second IR channel, highlights
    inline const juce::Colour accentDeep { 0xffb38600 };

    inline const juce::Colour textLabel { 0xffcfcfcf };
    inline const juce::Colour textValue { 0xffffffff };
    inline const juce::Colour textAxis { 0xff7a7a7a };
    inline const juce::Colour grid { 0x407a7a7a };

    inline const juce::Colour knobBody { 0xff1e1e1e };
    inline const juce::Colour knobHighlight { 0xff3c3c3c };
    inline const juce::Colour thumbTop { 0xff4a4a4a };
    inline const juce::Colour thumbBottom { 0xff1c1c1c };

    inline const juce::Colour error { 0xffff5a4f };
    inline const juce::Colour idle { 0xff5a5a5a };

    inline constexpr float panelCornerRadius = 8.0f;
}
