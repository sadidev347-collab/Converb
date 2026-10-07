#include <PluginProcessor.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    constexpr double sampleRate = 48000.0;

    void setParameter (PluginProcessor& p, const char* id, float value)
    {
        auto* param = p.getValueTreeState().getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float getParameter (PluginProcessor& p, const char* id)
    {
        return p.getValueTreeState().getRawParameterValue (id)->load();
    }

    // Loading is asynchronous; wait for the loader thread to finish
    void waitForIR (PluginProcessor& p)
    {
        for (int i = 0; i < 2000; ++i)
        {
            if (p.getIRDisplayData()->status != ir::DisplayData::Status::loading)
                return;

            juce::Thread::sleep (5);
        }

        FAIL ("Timed out waiting for the impulse response to load");
    }

    juce::AudioBuffer<float> makeNoise (int numChannels, int numSamples, int seed = 1)
    {
        juce::AudioBuffer<float> buffer (numChannels, numSamples);
        juce::Random random (seed);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, random.nextFloat() - 0.5f);

        return buffer;
    }

    juce::AudioBuffer<float> makeDecayingIR (int numChannels, int numSamples)
    {
        auto buffer = makeNoise (numChannels, numSamples, 7);

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * std::exp (-6.0f * (float) i / (float) numSamples));

        return buffer;
    }

    void process (PluginProcessor& p, juce::AudioBuffer<float>& buffer)
    {
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
    }

    void requireBuffersMatch (const juce::AudioBuffer<float>& actual, const juce::AudioBuffer<float>& expected, float tolerance)
    {
        REQUIRE (actual.getNumChannels() == expected.getNumChannels());
        REQUIRE (actual.getNumSamples() == expected.getNumSamples());

        auto worst = 0.0f;
        for (int ch = 0; ch < actual.getNumChannels(); ++ch)
            for (int i = 0; i < actual.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (actual.getSample (ch, i) - expected.getSample (ch, i)));

        CHECK (worst <= tolerance);
    }
}

TEST_CASE ("Gain laws", "[parameters]")
{
    CHECK (Parameters::reverbAmountToGain (0.0f) == 0.0f);
    CHECK (Parameters::reverbAmountToGain (1.0f) == 1.0f);
    CHECK (Parameters::reverbAmountToGain (0.5f) == Catch::Approx (0.25f));

    const auto dry = Parameters::mixToDryWet (0.0f);
    CHECK (dry.dry == 1.0f);
    CHECK (dry.wet == 0.0f);

    const auto wet = Parameters::mixToDryWet (1.0f);
    CHECK (wet.dry == 0.0f);
    CHECK (wet.wet == 1.0f);

    // Equal power across the range
    for (auto m : { 0.1f, 0.35f, 0.5f, 0.9f })
    {
        const auto g = Parameters::mixToDryWet (m);
        CHECK (g.dry * g.dry + g.wet * g.wet == Catch::Approx (1.0f));
    }
}

TEST_CASE ("Defaults", "[parameters]")
{
    PluginProcessor p;
    // APVTS stores normalised values, so 0 dB comes back with float rounding
    CHECK (getParameter (p, ParamIDs::inputGain) == Catch::Approx (0.0f).margin (1.0e-5));
    CHECK (getParameter (p, ParamIDs::reverbAmount) == 50.0f);
    CHECK (getParameter (p, ParamIDs::mix) == 35.0f);
    CHECK (getParameter (p, ParamIDs::outputGain) == Catch::Approx (0.0f).margin (1.0e-5));
    CHECK (p.getIRDisplayData()->status == ir::DisplayData::Status::empty);
}

TEST_CASE ("Mix 0 % passes the dry signal scaled only by the gains", "[dsp]")
{
    PluginProcessor p;
    setParameter (p, ParamIDs::mix, 0.0f);

    SECTION ("unity gains pass the input through")
    {
        p.prepareToPlay (sampleRate, 512);
        const auto input = makeNoise (2, 512);
        auto buffer = input;
        process (p, buffer);
        requireBuffersMatch (buffer, input, 1.0e-6f);
    }

    SECTION ("with an IR loaded and gains applied")
    {
        p.loadImpulseResponse (makeDecayingIR (2, 4800), sampleRate, "test");
        waitForIR (p);
        setParameter (p, ParamIDs::inputGain, 6.0f);
        setParameter (p, ParamIDs::outputGain, -3.0f);
        p.prepareToPlay (sampleRate, 512);

        const auto input = makeNoise (2, 512);
        auto expected = input;
        expected.applyGain (juce::Decibels::decibelsToGain (6.0f) * juce::Decibels::decibelsToGain (-3.0f));

        auto buffer = input;
        process (p, buffer);
        requireBuffersMatch (buffer, expected, 1.0e-6f);
    }
}

TEST_CASE ("Gain parameters produce the expected dB change", "[dsp]")
{
    PluginProcessor p;
    setParameter (p, ParamIDs::mix, 0.0f);

    for (auto [inputDb, outputDb] : { std::pair { 6.0f, 0.0f }, std::pair { 0.0f, -12.0f }, std::pair { -24.0f, 12.0f }, std::pair { 12.0f, -6.0f } })
    {
        setParameter (p, ParamIDs::inputGain, inputDb);
        setParameter (p, ParamIDs::outputGain, outputDb);
        p.prepareToPlay (sampleRate, 256);

        auto buffer = makeNoise (2, 256);
        const auto inputRms = buffer.getRMSLevel (0, 0, 256);
        process (p, buffer);
        const auto measuredDb = juce::Decibels::gainToDecibels (buffer.getRMSLevel (0, 0, 256) / inputRms);
        CHECK (measuredDb == Catch::Approx (inputDb + outputDb).margin (0.01));
    }
}

TEST_CASE ("Gain changes are smoothed", "[dsp]")
{
    PluginProcessor p;
    setParameter (p, ParamIDs::mix, 0.0f);
    p.prepareToPlay (sampleRate, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    process (p, buffer);

    // Jump +12 dB on a DC signal: output must ramp, not step
    setParameter (p, ParamIDs::outputGain, 12.0f);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.1f, 512);

    process (p, buffer);
    CHECK (buffer.getSample (0, 0) < 0.11f);
    CHECK (buffer.getSample (0, 511) > buffer.getSample (0, 0));

    for (int i = 1; i < 512; ++i)
        REQUIRE (buffer.getSample (0, i) - buffer.getSample (0, i - 1) < 0.01f);
}

TEST_CASE ("No IR loaded: the wet path is silent", "[dsp]")
{
    PluginProcessor p;
    setParameter (p, ParamIDs::mix, 100.0f);
    setParameter (p, ParamIDs::reverbAmount, 100.0f);
    p.prepareToPlay (sampleRate, 512);

    for (int block = 0; block < 4; ++block)
    {
        auto buffer = makeNoise (2, 512, block);
        process (p, buffer);
        CHECK (buffer.getMagnitude (0, 512) < 1.0e-6f);
    }
}

TEST_CASE ("Unit impulse IR at 100 % mix and amount reproduces the input", "[dsp]")
{
    PluginProcessor p;
    juce::AudioBuffer<float> impulse (1, 1);
    impulse.setSample (0, 0, 1.0f);
    p.loadImpulseResponse (std::move (impulse), sampleRate, "impulse");
    waitForIR (p);
    REQUIRE (p.getIRDisplayData()->status == ir::DisplayData::Status::loaded);

    setParameter (p, ParamIDs::mix, 100.0f);
    setParameter (p, ParamIDs::reverbAmount, 100.0f);
    p.prepareToPlay (sampleRate, 256);
    const auto latency = p.getLatencySamples();
    REQUIRE (latency == 0);

    // Include a block bigger than the prepared size to exercise chunking
    for (auto blockSize : { 256, 100, 1000, 256 })
    {
        const auto input = makeNoise (2, blockSize, blockSize);
        auto buffer = input;
        process (p, buffer);
        requireBuffersMatch (buffer, input, 1.0e-4f);
    }
}

TEST_CASE ("IR preparation", "[ir]")
{
    SECTION ("leading silence is trimmed")
    {
        auto buffer = makeDecayingIR (2, 4800);
        juce::AudioBuffer<float> padded (2, 4800 + 1000);
        padded.clear();
        for (int ch = 0; ch < 2; ++ch)
            padded.copyFrom (ch, 1000, buffer, ch, 0, 4800);

        const auto result = ir::trimAndCap (padded, sampleRate);
        CHECK (result.leadingSamplesRemoved >= 1000);
        CHECK (padded.getNumSamples() <= 4800);
        CHECK_FALSE (result.truncated);
    }

    SECTION ("length is capped")
    {
        auto buffer = makeNoise (1, (int) (sampleRate * 12.0));
        const auto result = ir::trimAndCap (buffer, sampleRate);
        CHECK (result.truncated);
        CHECK (buffer.getNumSamples() == (int) (sampleRate * ir::maxLengthSeconds));
    }

    SECTION ("normalisation gives unit energy in the loudest channel")
    {
        auto buffer = makeDecayingIR (2, 4800);
        buffer.applyGain (1, 0, 4800, 0.5f);
        buffer.applyGain (ir::normalisationGain (buffer));

        double energy = 0.0;
        for (int i = 0; i < 4800; ++i)
            energy += (double) buffer.getSample (0, i) * buffer.getSample (0, i);

        CHECK (energy == Catch::Approx (1.0).epsilon (1.0e-4));
    }

    SECTION ("FLAC state encoding round-trips")
    {
        auto buffer = makeDecayingIR (2, 4800);
        buffer.applyGain (1.7f); // over full scale, exercising the scale factor
        float scale = 0.0f;
        const auto flac = ir::encodeFlac (buffer, 44100.0, scale);
        REQUIRE (flac.getSize() > 0);

        juce::AudioBuffer<float> decoded;
        double decodedRate = 0.0;
        REQUIRE (ir::decodeFlac (flac, scale, decoded, decodedRate).wasOk());
        CHECK (decodedRate == 44100.0);
        requireBuffersMatch (decoded, buffer, 1.0e-5f);
    }
}

TEST_CASE ("Loading an IR from a file", "[ir]")
{
    const auto file = juce::File::createTempFile (".wav");
    {
        // Mono, 44.1 kHz, with 500 samples of leading silence
        juce::AudioBuffer<float> audio (1, 22050 + 500);
        audio.clear();
        auto ir = makeDecayingIR (1, 22050);
        audio.copyFrom (0, 500, ir, 0, 0, 22050);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    PluginProcessor p;
    p.loadImpulseResponse (file);
    waitForIR (p);

    const auto data = p.getIRDisplayData();
    REQUIRE (data->status == ir::DisplayData::Status::loaded);
    CHECK (data->name == file.getFileName());
    CHECK (data->numChannels == 1);
    CHECK (data->sampleRate == 44100.0);
    CHECK (data->lengthSeconds <= 0.5 + 1.0e-9);
    CHECK (data->peaks.size() == 1);
    CHECK (p.getTailLengthSeconds() == Catch::Approx (data->lengthSeconds));

    SECTION ("unreadable files report an error and keep the current IR")
    {
        const auto bogus = juce::File::createTempFile (".wav");
        bogus.replaceWithText ("not audio");
        p.loadImpulseResponse (bogus);
        waitForIR (p);

        const auto after = p.getIRDisplayData();
        CHECK (after->status == ir::DisplayData::Status::error);
        CHECK (after->message.isNotEmpty());
        CHECK (after->name == file.getFileName());
        bogus.deleteFile();
    }

    SECTION ("missing file and no embedded data shows IR missing")
    {
        juce::MemoryBlock state;
        p.getStateInformation (state);
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        REQUIRE (xml != nullptr);
        auto* irXml = xml->getChildByName ("IR");
        REQUIRE (irXml != nullptr);
        CHECK (irXml->getStringAttribute ("path") == file.getFullPathName());
        irXml->removeAttribute ("flac");
        juce::MemoryBlock stripped;
        juce::AudioProcessor::copyXmlToBinary (*xml, stripped);

        file.deleteFile();

        PluginProcessor restored;
        restored.setStateInformation (stripped.getData(), (int) stripped.getSize());
        waitForIR (restored);
        CHECK (restored.getIRDisplayData()->status == ir::DisplayData::Status::missing);
        CHECK (restored.getIRDisplayData()->name == file.getFileName());

        // Re-saving keeps the reference
        juce::MemoryBlock resaved;
        restored.getStateInformation (resaved);
        auto resavedXml = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize());
        REQUIRE (resavedXml->getChildByName ("IR") != nullptr);
        CHECK (resavedXml->getChildByName ("IR")->getStringAttribute ("path") == file.getFullPathName());
    }

    file.deleteFile();
}

TEST_CASE ("State round-trips parameters and the IR", "[state]")
{
    PluginProcessor original;
    setParameter (original, ParamIDs::inputGain, -4.5f);
    setParameter (original, ParamIDs::reverbAmount, 72.0f);
    setParameter (original, ParamIDs::mix, 18.0f);
    setParameter (original, ParamIDs::outputGain, 3.2f);
    original.loadImpulseResponse (makeDecayingIR (2, 22050), 44100.0, "Generated Hall");
    waitForIR (original);
    REQUIRE (original.getIRDisplayData()->status == ir::DisplayData::Status::loaded);

    juce::MemoryBlock state;
    original.getStateInformation (state);

    PluginProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    waitForIR (restored);

    CHECK (getParameter (restored, ParamIDs::inputGain) == Catch::Approx (-4.5f));
    CHECK (getParameter (restored, ParamIDs::reverbAmount) == Catch::Approx (72.0f));
    CHECK (getParameter (restored, ParamIDs::mix) == Catch::Approx (18.0f));
    CHECK (getParameter (restored, ParamIDs::outputGain) == Catch::Approx (3.2f));

    const auto a = original.getIRDisplayData();
    const auto b = restored.getIRDisplayData();
    REQUIRE (b->status == ir::DisplayData::Status::loaded);
    CHECK (b->name == a->name);
    CHECK (b->numChannels == a->numChannels);
    CHECK (juce::exactlyEqual (b->sampleRate, a->sampleRate));
    CHECK (b->lengthSeconds == Catch::Approx (a->lengthSeconds));
    CHECK (b->peakDb == Catch::Approx (a->peakDb).margin (0.01));
    CHECK (b->rmsDb == Catch::Approx (a->rmsDb).margin (0.01));

    // And both render the same audio
    for (auto* p : { &original, &restored })
        p->prepareToPlay (sampleRate, 512);

    for (int block = 0; block < 8; ++block)
    {
        auto x = makeNoise (2, 512, 100 + block);
        auto y = x;
        process (original, x);
        process (restored, y);
        requireBuffersMatch (y, x, 1.0e-4f);
    }
}

// Hidden: renders the editor to PNG for eyeballing the UI. Run with: ./Tests "[screenshot]"
// Writes to $CONVERB_SNAPSHOT_DIR (or the temp directory).
TEST_CASE ("Editor snapshot", "[.][screenshot]")
{
    const auto dirVar = juce::SystemStats::getEnvironmentVariable ("CONVERB_SNAPSHOT_DIR", {});
    const auto dir = dirVar.isNotEmpty() ? juce::File (dirVar) : juce::File::getSpecialLocation (juce::File::tempDirectory);

    PluginProcessor p;
    p.loadImpulseResponse (makeDecayingIR (2, 44100 * 2), 48000.0, "Generated Hall.wav");
    waitForIR (p);
    p.prepareToPlay (sampleRate, 512);

    for (int block = 0; block < 20; ++block)
    {
        auto buffer = makeNoise (2, 512, block);
        buffer.applyGain (0.5f);
        process (p, buffer);
    }

    for (auto envelope : { false, true })
    {
        p.getValueTreeState().state.setProperty ("irView", envelope ? 1 : 0, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());

        // let the meter timer tick once
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto file = dir.getChildFile (envelope ? "converb-envelope.png" : "converb-waveform.png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat png;
        REQUIRE (png.writeImageToStream (image, stream));

        p.editorBeingDeleted (editor.get());
    }
}
