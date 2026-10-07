#include "Parameters.h"

namespace
{
    juce::String gainToText (float db, int)
    {
        auto rounded = std::round (db * 10.0f) / 10.0f;

        if (std::abs (rounded) < 0.05f)
            rounded = 0.0f; // avoid "-0.0 dB"

        return (rounded > 0.0f ? "+" : "") + juce::String (rounded, 1) + " dB";
    }

    juce::String percentToText (float percent, int)
    {
        return juce::String (juce::roundToInt (percent)) + " %";
    }

    float textToFloat (const juce::String& text)
    {
        return text.retainCharacters ("-+.0123456789").getFloatValue();
    }

    std::unique_ptr<juce::AudioParameterFloat> makeGain (const char* id, const juce::String& name)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, Parameters::version },
            name,
            juce::NormalisableRange<float> { Parameters::minGainDb, Parameters::maxGainDb }, // continuous, so 0 dB is exact
            0.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction (gainToText)
                .withValueFromStringFunction (textToFloat));
    }

    std::unique_ptr<juce::AudioParameterFloat> makePercent (const char* id, const juce::String& name, float defaultValue)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, Parameters::version },
            name,
            juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f },
            defaultValue,
            juce::AudioParameterFloatAttributes()
                .withLabel ("%")
                .withStringFromValueFunction (percentToText)
                .withValueFromStringFunction (textToFloat));
    }
}

namespace Parameters
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        return {
            makeGain (ParamIDs::inputGain, "Input Gain"),
            makePercent (ParamIDs::reverbAmount, "Reverb Amount", defaultReverbAmount),
            makePercent (ParamIDs::mix, "Mix", defaultMix),
            makeGain (ParamIDs::outputGain, "Output Gain"),
        };
    }

    float reverbAmountToGain (float amount01) noexcept
    {
        const auto a = juce::jlimit (0.0f, 1.0f, amount01);
        return a * a;
    }

    DryWet mixToDryWet (float mix01) noexcept
    {
        const auto m = juce::jlimit (0.0f, 1.0f, mix01);

        if (m <= 0.0f)
            return { 1.0f, 0.0f };

        if (m >= 1.0f)
            return { 0.0f, 1.0f };

        const auto angle = m * juce::MathConstants<float>::halfPi;
        return { std::cos (angle), std::sin (angle) };
    }
}
