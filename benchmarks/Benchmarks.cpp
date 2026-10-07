TEST_CASE ("Boot performance")
{
    BENCHMARK_ADVANCED ("Processor constructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::storage_for<PluginProcessor>> storage (size_t (meter.runs()));
        meter.measure ([&] (int i) { storage[(size_t) i].construct(); });
    };

    BENCHMARK_ADVANCED ("Processor destructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::destructable_object<PluginProcessor>> storage (size_t (meter.runs()));
        for (auto& s : storage)
            s.construct();
        meter.measure ([&] (int i) { storage[(size_t) i].destruct(); });
    };

    BENCHMARK_ADVANCED ("Editor open and close")
    (Catch::Benchmark::Chronometer meter)
    {
        PluginProcessor plugin;

        // due to complex construction logic of the editor, let's measure open/close together
        meter.measure ([&] (int /* i */) {
            auto editor = plugin.createEditorAndMakeActive();
            plugin.editorBeingDeleted (editor);
            delete editor;
            return plugin.getActiveEditor();
        });
    };
}

TEST_CASE ("Processing performance")
{
    // A dense 5 s stereo IR at 48 kHz, processed in 512-sample blocks (~10.7 ms of audio each)
    PluginProcessor plugin;
    juce::AudioBuffer<float> ir (2, 48000 * 5);
    juce::Random random (1);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < ir.getNumSamples(); ++i)
            ir.setSample (ch, i, (random.nextFloat() - 0.5f) * std::exp (-3.0f * (float) i / (float) ir.getNumSamples()));

    plugin.loadImpulseResponse (std::move (ir), 48000.0, "bench");

    for (int i = 0; i < 2000 && plugin.getIRDisplayData()->status == ir::DisplayData::Status::loading; ++i)
        juce::Thread::sleep (5);

    plugin.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    BENCHMARK ("processBlock, 512 samples, 5 s stereo IR")
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, random.nextFloat() - 0.5f);

        plugin.processBlock (buffer, midi);
        return buffer.getSample (0, 0);
    };
}
