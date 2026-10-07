#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "Parameters.h"
#include "dsp/IRMailbox.h"
#include "dsp/ImpulseResponse.h"

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    juce::AudioFormatManager& getFormatManager() noexcept { return formatManager; }

    // Asynchronous: decoding and preparation happen on the loader thread. Call from any non-audio thread.
    void loadImpulseResponse (const juce::File& file);
    void loadImpulseResponse (juce::AudioBuffer<float> buffer, double sampleRate, const juce::String& name);
    void clearImpulseResponse();

    // Thread-safe snapshot for the UI; compare getIRDisplayVersion() to know when to re-fetch.
    std::shared_ptr<const ir::DisplayData> getIRDisplayData() const;
    int getIRDisplayVersion() const noexcept { return irDisplayVersion.load(); }

    // Per-channel peak levels (linear) since the last call. Message thread.
    std::array<float, 2> takeInputPeaks() noexcept;
    std::array<float, 2> takeOutputPeaks() noexcept;

private:
    struct StoredIR
    {
        juce::String name, path;
        juce::MemoryBlock flac;
        float flacScale = 1.0f;
    };

    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;
    void installPendingImpulseResponse() noexcept;
    void updateSmoothingTargets() noexcept;

    // loader thread
    void prepareAndPublish (juce::AudioBuffer<float> buffer, double sampleRate, const juce::String& name, const juce::String& path, int generation);
    void publishStatus (ir::DisplayData::Status status, const juce::String& name, const juce::String& path, const juce::String& message);
    void addLoaderJob (std::function<void (int generation)> work);
    bool isStale (int generation) const noexcept { return generation != loadGeneration.load(); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* reverbAmountParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;

    // Zero-latency, two-stage partitioned convolution (a short uniform head, longer tail partitions)
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 512 } };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1 };
    IRMailbox mailbox;

    juce::SmoothedValue<float> inputGain, outputGain, dryGain, wetGain;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> gainRamps; // 0: input, 1: dry, 2: wet, 3: output
    int maxChunkSize = 0;
    int latencySamples = 0;

    std::array<std::atomic<float>, 2> inputPeaks {}, outputPeaks {};
    std::atomic<double> tailSeconds { 0.0 };

    juce::AudioFormatManager formatManager;

    mutable std::mutex irStateMutex; // never taken on the audio thread
    StoredIR storedIR;
    std::shared_ptr<const ir::DisplayData> irDisplayData;
    std::atomic<int> irDisplayVersion { 0 };
    std::atomic<int> loadGeneration { 0 };

    // Declared last so it is destroyed (and its job finished) before everything it touches
    juce::ThreadPool loaderPool { juce::ThreadPoolOptions().withNumberOfThreads (1).withThreadName ("Converb IR loader") };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
