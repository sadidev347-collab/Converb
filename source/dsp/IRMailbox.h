#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Hands fully-prepared impulse responses from the loader thread to the audio thread without
// locks or audio-thread allocation/deallocation.
//
// - post() (loader thread only) publishes a buffer, replacing any not-yet-taken one.
// - take() (audio thread) grabs the latest buffer, if any. The audio thread moves the samples
//   out and hands the empty shell back through recycle(), so it never frees memory itself.
// - collectGarbage() (loader thread only) frees recycled shells.
class IRMailbox
{
public:
    struct Item
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 0.0;
    };

    IRMailbox() = default;

    ~IRMailbox()
    {
        delete incoming.exchange (nullptr);
        collectGarbage();
    }

    void post (std::unique_ptr<Item> item)
    {
        collectGarbage();
        delete incoming.exchange (item.release(), std::memory_order_acq_rel);
    }

    Item* take() noexcept
    {
        return incoming.exchange (nullptr, std::memory_order_acq_rel);
    }

    void recycle (Item* item) noexcept
    {
        int start1, size1, start2, size2;
        spentFifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 > 0)
        {
            spent[(size_t) start1] = item;
            spentFifo.finishedWrite (1);
        }
        else
        {
            // Only reachable if the loader never collects; freeing an empty shell is the lesser evil
            delete item;
        }
    }

    void collectGarbage()
    {
        int start1, size1, start2, size2;
        const auto ready = spentFifo.getNumReady();
        spentFifo.prepareToRead (ready, start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i)
            delete std::exchange (spent[(size_t) (start1 + i)], nullptr);

        for (int i = 0; i < size2; ++i)
            delete std::exchange (spent[(size_t) (start2 + i)], nullptr);

        spentFifo.finishedRead (size1 + size2);
    }

private:
    static constexpr int spentCapacity = 32;

    std::atomic<Item*> incoming { nullptr };
    juce::AbstractFifo spentFifo { spentCapacity };
    std::array<Item*, spentCapacity> spent {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRMailbox)
};
