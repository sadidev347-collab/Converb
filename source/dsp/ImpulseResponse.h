#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

// Impulse response preparation. Everything here is meant for a background (loader) thread:
// it allocates, does file I/O and iterates whole IRs.
namespace ir
{
    inline constexpr double maxLengthSeconds = 10.0;
    inline constexpr float leadingSilenceThresholdDb = -60.0f; // relative to the IR's peak
    inline constexpr int maxChannels = 2;
    inline constexpr int numDisplayBins = 2048;

    // Immutable snapshot the UI draws from. Built on the loader thread, never touched by audio.
    struct DisplayData
    {
        enum class Status
        {
            empty,
            loading,
            loaded,
            missing,
            error
        };

        Status status = Status::empty;
        juce::String name, path, message;

        double sampleRate = 0.0;
        double lengthSeconds = 0.0;
        int numChannels = 0;
        float rmsDb = -100.0f;
        float peakDb = -100.0f;
        bool wasTruncated = false;

        // [channel][bin] min/max of the un-normalised IR, numDisplayBins bins spread over the whole length
        std::vector<std::vector<juce::Range<float>>> peaks;
    };

    // Reads up to maxChannels channels and a little over maxLengthSeconds (so leading silence can be trimmed).
    juce::Result readAudio (std::unique_ptr<juce::AudioFormatReader> reader, juce::AudioBuffer<float>& out, double& sampleRate);
    juce::Result readFile (const juce::File& file, juce::AudioFormatManager& formats, juce::AudioBuffer<float>& out, double& sampleRate);

    struct TrimResult
    {
        int leadingSamplesRemoved = 0;
        bool truncated = false;
    };

    // Trims leading silence and caps the length to maxLengthSeconds, in place.
    TrimResult trimAndCap (juce::AudioBuffer<float>& buffer, double sampleRate);

    // Gain that brings the loudest channel to unit energy (sum of squares == 1).
    float normalisationGain (const juce::AudioBuffer<float>& buffer);

    DisplayData makeDisplayData (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::String& name, const juce::String& path);

    // Compact state encoding: 24-bit FLAC. Samples beyond full scale are scaled down; pass the
    // returned scale to decodeFlac to undo it.
    juce::MemoryBlock encodeFlac (const juce::AudioBuffer<float>& buffer, double sampleRate, float& scaleOut);
    juce::Result decodeFlac (const juce::MemoryBlock& data, float scale, juce::AudioBuffer<float>& out, double& sampleRate);
}
