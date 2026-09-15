#pragma once
#include <JuceHeader.h>
#include <array>

// Registro global compartido, en memoria, entre todas las instancias del plugin
// que viven en el mismo proceso de la DAW (una instancia = una pista/canal).
// Cada instancia publica la energia de su propio espectro en 6 bandas cada
// pocos milisegundos, y consulta la de las demas para saber donde esta
// compitiendo por espacio de frecuencia.

namespace encaje
{

constexpr int kNumBands = 6;
constexpr int kMaxSlots = 64;

struct BandInfo
{
    float lo, hi;
    const char* name;
};

static const std::array<BandInfo, kNumBands> kBands { {
    { 20.0f,   60.0f,   "Sub" },
    { 60.0f,   250.0f,  "Graves" },
    { 250.0f,  500.0f,  "Med-bajo" },
    { 500.0f,  2000.0f, "Medios" },
    { 2000.0f, 6000.0f, "Med-alto" },
    { 6000.0f, 16000.0f,"Agudos" }
} };

inline float bandCenterFreq (int i) noexcept
{
    return std::sqrt (kBands[(size_t) i].lo * kBands[(size_t) i].hi);
}

inline float bandQ (int i) noexcept
{
    auto c = bandCenterFreq (i);
    auto bw = kBands[(size_t) i].hi - kBands[(size_t) i].lo;
    return juce::jlimit (0.5f, 5.0f, c / juce::jmax (1.0f, bw));
}

class CarveRegistry
{
public:
    CarveRegistry() = default;

    int acquireSlot (int group)
    {
        const juce::ScopedLock sl (lock);
        for (int i = 0; i < kMaxSlots; ++i)
        {
            if (! slots[(size_t) i].active)
            {
                slots[(size_t) i].active = true;
                slots[(size_t) i].group = group;
                slots[(size_t) i].bandEnergy.fill (0.0f);
                slots[(size_t) i].lastUpdateMs = juce::Time::getMillisecondCounter();
                return i;
            }
        }
        return -1;
    }

    void releaseSlot (int slot)
    {
        if (slot < 0) return;
        const juce::ScopedLock sl (lock);
        slots[(size_t) slot].active = false;
    }

    void publish (int slot, int group, const std::array<float, kNumBands>& bandEnergy)
    {
        if (slot < 0) return;
        const juce::ScopedLock sl (lock);
        slots[(size_t) slot].group = group;
        slots[(size_t) slot].bandEnergy = bandEnergy;
        slots[(size_t) slot].lastUpdateMs = juce::Time::getMillisecondCounter();
    }

    // Para cada banda: que fraccion del total le corresponde a `slot`,
    // y cuantas pistas del mismo grupo compiten de verdad ahi (>15% del total).
    void computeShares (int slot, int group,
                         std::array<float, kNumBands>& outShares,
                         std::array<int, kNumBands>& outCompetitors) const
    {
        const juce::ScopedLock sl (lock);
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        const juce::uint32 staleMs = 2000;

        for (int b = 0; b < kNumBands; ++b)
        {
            float total = 0.0f;
            float mine = 0.0f;

            for (int i = 0; i < kMaxSlots; ++i)
            {
                const auto& s = slots[(size_t) i];
                if (! s.active || s.group != group) continue;
                if (now - s.lastUpdateMs > staleMs) continue;
                total += s.bandEnergy[(size_t) b];
                if (i == slot) mine = s.bandEnergy[(size_t) b];
            }

            int competitors = 0;
            if (total > 1.0e-9f)
            {
                for (int i = 0; i < kMaxSlots; ++i)
                {
                    const auto& s = slots[(size_t) i];
                    if (! s.active || s.group != group) continue;
                    if (now - s.lastUpdateMs > staleMs) continue;
                    if ((s.bandEnergy[(size_t) b] / total) > 0.15f) competitors++;
                }
            }

            outShares[(size_t) b] = total > 1.0e-9f ? mine / total : 0.0f;
            outCompetitors[(size_t) b] = competitors;
        }
    }

private:
    struct Slot
    {
        bool active = false;
        int group = 0;
        std::array<float, kNumBands> bandEnergy {};
        juce::uint32 lastUpdateMs = 0;
    };

    mutable juce::CriticalSection lock;
    std::array<Slot, kMaxSlots> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CarveRegistry)
};

} // namespace encaje
