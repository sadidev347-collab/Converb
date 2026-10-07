#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
    inline constexpr auto inputGain = "inputGain";
    inline constexpr auto reverbAmount = "reverbAmount";
    inline constexpr auto mix = "mix";
    inline constexpr auto outputGain = "outputGain";
}

namespace Parameters
{
    inline constexpr int version = 1;

    inline constexpr float minGainDb = -24.0f;
    inline constexpr float maxGainDb = 12.0f;
    inline constexpr float defaultReverbAmount = 50.0f; // percent
    inline constexpr float defaultMix = 35.0f; // percent

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // 0..1 -> linear wet gain using a perceptual (squared) curve: 0 = -inf, 1 = 0 dB
    float reverbAmountToGain (float amount01) noexcept;

    struct DryWet
    {
        float dry, wet;
    };

    // 0..1 -> equal-power dry/wet gains (mix 0 is exactly dry = 1, wet = 0)
    DryWet mixToDryWet (float mix01) noexcept;
}
