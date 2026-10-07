#include "ImpulseResponse.h"

namespace ir
{
    juce::Result readAudio (std::unique_ptr<juce::AudioFormatReader> reader, juce::AudioBuffer<float>& out, double& sampleRate)
    {
        if (reader == nullptr)
            return juce::Result::fail ("Unsupported or unreadable audio file");

        if (reader->sampleRate <= 0.0 || reader->numChannels == 0)
            return juce::Result::fail ("File has no audio");

        if (reader->lengthInSamples <= 0)
            return juce::Result::fail ("File is empty");

        // Leave some headroom past the cap so leading silence can still be trimmed away
        const auto maxSamplesToRead = (juce::int64) ((maxLengthSeconds + 5.0) * reader->sampleRate);
        const auto numSamples = (int) std::min (reader->lengthInSamples, maxSamplesToRead);
        const auto numChannels = std::min ((int) reader->numChannels, maxChannels);

        out.setSize (numChannels, numSamples);

        if (! reader->read (out.getArrayOfWritePointers(), numChannels, 0, numSamples))
            return juce::Result::fail ("Could not read audio data");

        sampleRate = reader->sampleRate;
        return juce::Result::ok();
    }

    juce::Result readFile (const juce::File& file, juce::AudioFormatManager& formats, juce::AudioBuffer<float>& out, double& sampleRate)
    {
        if (! file.existsAsFile())
            return juce::Result::fail ("File not found");

        return readAudio (std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)), out, sampleRate);
    }

    TrimResult trimAndCap (juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        TrimResult result;
        const auto numSamples = buffer.getNumSamples();
        const auto numChannels = buffer.getNumChannels();

        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = std::max (peak, buffer.getMagnitude (ch, 0, numSamples));

        if (peak <= 0.0f)
            return result; // all silence: nothing sensible to trim against

        const auto threshold = peak * juce::Decibels::decibelsToGain (leadingSilenceThresholdDb);
        auto firstAudible = numSamples;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);

            for (int i = 0; i < firstAudible; ++i)
            {
                if (std::abs (data[i]) > threshold)
                {
                    firstAudible = i;
                    break;
                }
            }
        }

        const auto maxSamples = (int) std::floor (maxLengthSeconds * sampleRate);
        const auto newLength = std::min (numSamples - firstAudible, maxSamples);
        result.leadingSamplesRemoved = firstAudible;
        result.truncated = (numSamples - firstAudible) > maxSamples;

        if (firstAudible > 0 || newLength < numSamples)
        {
            juce::AudioBuffer<float> trimmed (numChannels, newLength);

            for (int ch = 0; ch < numChannels; ++ch)
                trimmed.copyFrom (ch, 0, buffer, ch, firstAudible, newLength);

            buffer = std::move (trimmed);
        }

        return result;
    }

    float normalisationGain (const juce::AudioBuffer<float>& buffer)
    {
        double maxEnergy = 0.0;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            double energy = 0.0;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                energy += (double) data[i] * (double) data[i];

            maxEnergy = std::max (maxEnergy, energy);
        }

        if (maxEnergy < 1.0e-12)
            return 1.0f;

        return (float) (1.0 / std::sqrt (maxEnergy));
    }

    DisplayData makeDisplayData (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::String& name, const juce::String& path)
    {
        DisplayData d;
        d.status = DisplayData::Status::loaded;
        d.name = name;
        d.path = path;
        d.sampleRate = sampleRate;
        d.numChannels = buffer.getNumChannels();

        const auto numSamples = buffer.getNumSamples();
        d.lengthSeconds = sampleRate > 0.0 ? numSamples / sampleRate : 0.0;

        double sumSquares = 0.0;
        float peak = 0.0f;
        d.peaks.resize ((size_t) d.numChannels);

        for (int ch = 0; ch < d.numChannels; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            auto& bins = d.peaks[(size_t) ch];
            bins.resize ((size_t) numDisplayBins);

            for (int bin = 0; bin < numDisplayBins; ++bin)
            {
                const auto start = (int) ((juce::int64) bin * numSamples / numDisplayBins);
                const auto end = std::max (start + 1, (int) ((juce::int64) (bin + 1) * numSamples / numDisplayBins));
                auto lo = 0.0f, hi = 0.0f;

                for (int i = start; i < std::min (end, numSamples); ++i)
                {
                    lo = std::min (lo, data[i]);
                    hi = std::max (hi, data[i]);
                }

                bins[(size_t) bin] = { lo, hi };
            }

            for (int i = 0; i < numSamples; ++i)
            {
                sumSquares += (double) data[i] * (double) data[i];
                peak = std::max (peak, std::abs (data[i]));
            }
        }

        const auto totalSamples = (double) numSamples * std::max (1, d.numChannels);
        const auto rms = totalSamples > 0.0 ? std::sqrt (sumSquares / totalSamples) : 0.0;
        d.rmsDb = juce::Decibels::gainToDecibels ((float) rms, -100.0f);
        d.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
        return d;
    }

    juce::MemoryBlock encodeFlac (const juce::AudioBuffer<float>& buffer, double sampleRate, float& scaleOut)
    {
        juce::MemoryBlock result;
        scaleOut = 1.0f;

        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = std::max (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

        // keep the integer encoding from clipping float files that exceed full scale
        if (peak > 0.999f)
            scaleOut = peak / 0.999f;

        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (result, false);

        const auto options = juce::AudioFormatWriterOptions()
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (buffer.getNumChannels())
                                 .withBitsPerSample (24);

        if (auto writer = flac.createWriterFor (stream, options))
        {
            if (juce::approximatelyEqual (scaleOut, 1.0f))
            {
                writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
            }
            else
            {
                juce::AudioBuffer<float> scaled (buffer);
                scaled.applyGain (1.0f / scaleOut);
                writer->writeFromAudioSampleBuffer (scaled, 0, scaled.getNumSamples());
            }
        }

        // the writer (and its stream) must be gone before the block is complete
        return result;
    }

    juce::Result decodeFlac (const juce::MemoryBlock& data, float scale, juce::AudioBuffer<float>& out, double& sampleRate)
    {
        juce::FlacAudioFormat flac;
        auto* reader = flac.createReaderFor (new juce::MemoryInputStream (data, false), true);
        auto result = readAudio (std::unique_ptr<juce::AudioFormatReader> (reader), out, sampleRate);

        if (result.wasOk() && scale > 0.0f)
            out.applyGain (scale);

        return result;
    }
}
