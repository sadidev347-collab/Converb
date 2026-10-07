#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr double smoothingSeconds = 0.03;
    constexpr auto irStateTag = "IR";

    std::unique_ptr<IRMailbox::Item> makeSilentIR()
    {
        // JUCE's engine defaults to a unit impulse (wet == dry); a silent IR makes "no IR" mean "no wet"
        auto item = std::make_unique<IRMailbox::Item>();
        item->buffer.setSize (1, 1);
        item->buffer.clear();
        item->sampleRate = 48000.0;
        return item;
    }

    void storePeak (std::atomic<float>& target, float value) noexcept
    {
        auto current = target.load (std::memory_order_relaxed);
        while (value > current && ! target.compare_exchange_weak (current, value, std::memory_order_relaxed))
        {
        }
    }
}

//==============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Converb", Parameters::createLayout())
{
    inputGainParam = apvts.getRawParameterValue (ParamIDs::inputGain);
    reverbAmountParam = apvts.getRawParameterValue (ParamIDs::reverbAmount);
    mixParam = apvts.getRawParameterValue (ParamIDs::mix);
    outputGainParam = apvts.getRawParameterValue (ParamIDs::outputGain);

    formatManager.registerBasicFormats();

    irDisplayData = std::make_shared<const ir::DisplayData>();

    // Nothing is processing yet, so it's fine to hand this straight to the engine
    auto silent = makeSilentIR();
    convolution.loadImpulseResponse (std::move (silent->buffer), silent->sampleRate, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
}

PluginProcessor::~PluginProcessor()
{
    loadGeneration.fetch_add (1); // tell any running job to bail out
    loaderPool.removeAllJobs (true, 10000);
}

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
    return false;
}

bool PluginProcessor::producesMidi() const
{
    return false;
}

bool PluginProcessor::isMidiEffect() const
{
    return false;
}

double PluginProcessor::getTailLengthSeconds() const
{
    return tailSeconds.load();
}

int PluginProcessor::getNumPrograms()
{
    return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
              // so this should be at least 1, even if you're not really implementing programs.
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String PluginProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    // Steinberg's VST3 validator fails plugins whose single default program has no name
    return "Default";
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto numChannels = (juce::uint32) std::max (1, getTotalNumOutputChannels());
    maxChunkSize = std::max (1, samplesPerBlock);

    // Make the most recently loaded IR active right away (prepare() builds it synchronously)
    installPendingImpulseResponse();

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxChunkSize, numChannels };
    convolution.prepare (spec);

    latencySamples = convolution.getLatency();
    setLatencySamples (latencySamples);

    dryDelay.prepare (spec);
    dryDelay.setMaximumDelayInSamples (std::max (1, latencySamples));
    dryDelay.setDelay ((float) latencySamples);

    dryBuffer.setSize ((int) numChannels, maxChunkSize);
    gainRamps.setSize (4, maxChunkSize);

    for (auto* smoother : { &inputGain, &outputGain, &dryGain, &wetGain })
        smoother->reset (sampleRate, smoothingSeconds);

    updateSmoothingTargets();

    for (auto* smoother : { &inputGain, &outputGain, &dryGain, &wetGain })
        smoother->setCurrentAndTargetValue (smoother->getTargetValue());
}

void PluginProcessor::releaseResources()
{
    convolution.reset();
    dryDelay.reset();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono or stereo, with matching input and output
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void PluginProcessor::installPendingImpulseResponse() noexcept
{
    if (auto* item = mailbox.take())
    {
        // Wait-free: the buffer is moved into JUCE's queue and built on its background thread
        convolution.loadImpulseResponse (std::move (item->buffer),
            item->sampleRate,
            juce::dsp::Convolution::Stereo::yes,
            juce::dsp::Convolution::Trim::no,
            juce::dsp::Convolution::Normalise::no);
        mailbox.recycle (item);
    }
}

void PluginProcessor::updateSmoothingTargets() noexcept
{
    const auto dryWet = Parameters::mixToDryWet (mixParam->load() / 100.0f);
    const auto amountGain = Parameters::reverbAmountToGain (reverbAmountParam->load() / 100.0f);

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));
    dryGain.setTargetValue (dryWet.dry);
    wetGain.setTargetValue (dryWet.wet * amountGain);
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    installPendingImpulseResponse();
    updateSmoothingTargets();

    // Hosts may exceed the block size given to prepareToPlay, so work in chunks we have room for
    for (int start = 0; start < buffer.getNumSamples(); start += maxChunkSize)
        processChunk (buffer, start, std::min (maxChunkSize, buffer.getNumSamples() - start));
}

void PluginProcessor::processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    const auto numChannels = std::min (buffer.getNumChannels(), dryBuffer.getNumChannels());

    auto* inRamp = gainRamps.getWritePointer (0);
    auto* dryRamp = gainRamps.getWritePointer (1);
    auto* wetRamp = gainRamps.getWritePointer (2);
    auto* outRamp = gainRamps.getWritePointer (3);

    for (int i = 0; i < numSamples; ++i)
    {
        inRamp[i] = inputGain.getNextValue();
        dryRamp[i] = dryGain.getNextValue();
        wetRamp[i] = wetGain.getNextValue();
        outRamp[i] = outputGain.getNextValue();
    }

    // Input gain, then keep a (latency-aligned) copy as the dry signal
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch, startSample);
        juce::FloatVectorOperations::multiply (data, inRamp, numSamples);
        storePeak (inputPeaks[(size_t) std::min (ch, 1)], juce::FloatVectorOperations::findMaximum (data, numSamples));
        storePeak (inputPeaks[(size_t) std::min (ch, 1)], -juce::FloatVectorOperations::findMinimum (data, numSamples));

        auto* dry = dryBuffer.getWritePointer (ch);

        if (latencySamples > 0)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                dryDelay.pushSample (ch, data[i]);
                dry[i] = dryDelay.popSample (ch);
            }
        }
        else
        {
            juce::FloatVectorOperations::copy (dry, data, numSamples);
        }
    }

    // Wet: convolution, in place
    {
        auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, (size_t) numChannels).getSubBlock ((size_t) startSample, (size_t) numSamples);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // Mix (wet ramp already includes the reverb amount), then output gain
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch, startSample);
        juce::FloatVectorOperations::multiply (data, wetRamp, numSamples);
        juce::FloatVectorOperations::addWithMultiply (data, dryBuffer.getReadPointer (ch), dryRamp, numSamples);
        juce::FloatVectorOperations::multiply (data, outRamp, numSamples);

        storePeak (outputPeaks[(size_t) std::min (ch, 1)], juce::FloatVectorOperations::findMaximum (data, numSamples));
        storePeak (outputPeaks[(size_t) std::min (ch, 1)], -juce::FloatVectorOperations::findMinimum (data, numSamples));
    }

    // Mono: mirror the meters so both bars move
    if (numChannels == 1)
    {
        storePeak (inputPeaks[1], inputPeaks[0].load (std::memory_order_relaxed));
        storePeak (outputPeaks[1], outputPeaks[0].load (std::memory_order_relaxed));
    }
}

std::array<float, 2> PluginProcessor::takeInputPeaks() noexcept
{
    return { inputPeaks[0].exchange (0.0f), inputPeaks[1].exchange (0.0f) };
}

std::array<float, 2> PluginProcessor::takeOutputPeaks() noexcept
{
    return { outputPeaks[0].exchange (0.0f), outputPeaks[1].exchange (0.0f) };
}

//==============================================================================
void PluginProcessor::addLoaderJob (std::function<void (int generation)> work)
{
    const auto generation = loadGeneration.fetch_add (1) + 1;
    loaderPool.addJob ([this, generation, job = std::move (work)] {
        if (! isStale (generation))
            job (generation);
    });
}

void PluginProcessor::loadImpulseResponse (const juce::File& file)
{
    publishStatus (ir::DisplayData::Status::loading, {}, {}, "Loading " + file.getFileName() + "...");

    addLoaderJob ([this, file] (int generation) {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 0.0;
        const auto result = ir::readFile (file, formatManager, buffer, sampleRate);

        if (isStale (generation))
            return;

        if (result.failed())
        {
            publishStatus (ir::DisplayData::Status::error, file.getFileName(), file.getFullPathName(), "Couldn't load " + file.getFileName() + ": " + result.getErrorMessage());
            return;
        }

        prepareAndPublish (std::move (buffer), sampleRate, file.getFileName(), file.getFullPathName(), generation);
    });
}

void PluginProcessor::loadImpulseResponse (juce::AudioBuffer<float> buffer, double sampleRate, const juce::String& name)
{
    publishStatus (ir::DisplayData::Status::loading, {}, {}, "Loading " + name + "...");

    addLoaderJob ([this, b = std::move (buffer), sampleRate, name] (int generation) mutable {
        prepareAndPublish (std::move (b), sampleRate, name, {}, generation);
    });
}

void PluginProcessor::clearImpulseResponse()
{
    addLoaderJob ([this] (int) {
        mailbox.post (makeSilentIR());
        tailSeconds = 0.0;

        {
            const std::scoped_lock lock (irStateMutex);
            storedIR = {};
        }

        publishStatus (ir::DisplayData::Status::empty, {}, {}, {});
    });
}

void PluginProcessor::prepareAndPublish (juce::AudioBuffer<float> buffer, double sampleRate, const juce::String& name, const juce::String& path, int generation)
{
    const auto trim = ir::trimAndCap (buffer, sampleRate);
    const auto normGain = ir::normalisationGain (buffer);

    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
    {
        publishStatus (ir::DisplayData::Status::error, name, path, "Couldn't load " + name + ": the file is empty");
        return;
    }

    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = std::max (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    if (peak <= 0.0f)
    {
        publishStatus (ir::DisplayData::Status::error, name, path, "Couldn't load " + name + ": the impulse response is silent");
        return;
    }

    auto display = ir::makeDisplayData (buffer, sampleRate, name, path);
    display.wasTruncated = trim.truncated;

    StoredIR stored;
    stored.name = name;
    stored.path = path;

    stored.flac = ir::encodeFlac (buffer, sampleRate, stored.flacScale);

    if (isStale (generation))
        return;

    auto item = std::make_unique<IRMailbox::Item>();
    item->buffer = std::move (buffer);
    item->buffer.applyGain (normGain);
    item->sampleRate = sampleRate;

    tailSeconds = display.lengthSeconds;
    mailbox.post (std::move (item));

    {
        const std::scoped_lock lock (irStateMutex);
        storedIR = std::move (stored);
        irDisplayData = std::make_shared<const ir::DisplayData> (std::move (display));
    }

    irDisplayVersion.fetch_add (1);
}

void PluginProcessor::publishStatus (ir::DisplayData::Status status, const juce::String& name, const juce::String& path, const juce::String& message)
{
    {
        const std::scoped_lock lock (irStateMutex);
        std::shared_ptr<ir::DisplayData> d;

        if (status == ir::DisplayData::Status::empty || status == ir::DisplayData::Status::missing)
        {
            d = std::make_shared<ir::DisplayData>();
            d->name = name;
            d->path = path;
        }
        else
        {
            // While loading, or after a failed load, the previous IR stays active, so keep showing it
            d = std::make_shared<ir::DisplayData> (*irDisplayData);
        }

        d->status = status;
        d->message = message;
        irDisplayData = std::move (d);
    }

    irDisplayVersion.fetch_add (1);
}

std::shared_ptr<const ir::DisplayData> PluginProcessor::getIRDisplayData() const
{
    const std::scoped_lock lock (irStateMutex);
    return irDisplayData;
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml = apvts.copyState().createXml();

    if (xml == nullptr)
        return;

    {
        const std::scoped_lock lock (irStateMutex);

        if (storedIR.name.isNotEmpty() || storedIR.path.isNotEmpty())
        {
            auto* irXml = xml->createNewChildElement (irStateTag);
            irXml->setAttribute ("name", storedIR.name);
            irXml->setAttribute ("path", storedIR.path);

            if (storedIR.flac.getSize() > 0)
            {
                irXml->setAttribute ("scale", storedIR.flacScale);
                irXml->setAttribute ("flac", storedIR.flac.toBase64Encoding());
            }
        }
    }

    copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    juce::String name, path, flacBase64;
    float scale = 1.0f;

    if (auto* irXml = xml->getChildByName (irStateTag))
    {
        name = irXml->getStringAttribute ("name");
        path = irXml->getStringAttribute ("path");
        flacBase64 = irXml->getStringAttribute ("flac");
        scale = (float) irXml->getDoubleAttribute ("scale", 1.0);
        xml->removeChildElement (irXml, true);
    }

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    if (name.isEmpty() && path.isEmpty())
    {
        clearImpulseResponse();
        return;
    }

    publishStatus (ir::DisplayData::Status::loading, {}, {}, "Loading " + name + "...");

    addLoaderJob ([this, name, path, flacBase64, scale] (int generation) {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 0.0;
        auto result = juce::Result::fail ("No embedded impulse response");

        // Prefer the embedded copy (exactly what was saved); fall back to the original file
        if (flacBase64.isNotEmpty())
        {
            juce::MemoryBlock flac;

            if (flac.fromBase64Encoding (flacBase64))
                result = ir::decodeFlac (flac, scale, buffer, sampleRate);
        }

        if (result.failed() && path.isNotEmpty())
            result = ir::readFile (juce::File (path), formatManager, buffer, sampleRate);

        if (isStale (generation))
            return;

        if (result.failed())
        {
            mailbox.post (makeSilentIR());
            tailSeconds = 0.0;

            {
                // keep the reference so re-saving the project doesn't lose it
                const std::scoped_lock lock (irStateMutex);
                storedIR = {};
                storedIR.name = name;
                storedIR.path = path;
            }

            publishStatus (ir::DisplayData::Status::missing, name, path, "IR missing: " + name);
            return;
        }

        prepareAndPublish (std::move (buffer), sampleRate, name, path, generation);
    });
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
