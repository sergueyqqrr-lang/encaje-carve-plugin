#pragma once
#include <JuceHeader.h>
#include <array>

// Registro global compartido, en memoria, entre todas las instancias del plugin
// que viven en el mismo proceso de la DAW (una instancia = una pista/canal).
// Cada instancia publica la energia de su propio espectro en 6 bandas, junto
// con la prioridad que le asigno el usuario. A partir de eso se decide, banda
// por banda, QUIEN manda ahi: ese se queda intacto y los demas se apartan.

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

// Lo que cada pista necesita saber de una banda para decidir su recorte.
struct BandVerdict
{
    float share       = 0.0f;  // fraccion de la energia de la banda que es mia (0..1)
    float leaderShare = 0.0f;  // fraccion que ocupa el dueno de la banda (0..1)
    int   competitors = 0;     // cuantas pistas ocupan esa banda de forma relevante
    bool  amDominant  = false; // soy yo quien manda en esta banda?
};

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
                slots[(size_t) i].priorityDb = 0.0f;
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

    void publish (int slot, int group, float priorityDb,
                  const std::array<float, kNumBands>& bandEnergy)
    {
        if (slot < 0) return;
        const juce::ScopedLock sl (lock);
        auto& s = slots[(size_t) slot];
        s.group = group;
        s.priorityDb = priorityDb;
        s.bandEnergy = bandEnergy;
        s.lastUpdateMs = juce::Time::getMillisecondCounter();
    }

    // Para cada banda decide quien es el dueno y devuelve el veredicto
    // desde el punto de vista de `slot`.
    //
    // El "peso" de una pista en una banda no es solo su energia: se multiplica
    // por la prioridad que le puso el usuario. Asi, si marcas la voz como
    // protagonista, gana la banda de medios aunque la guitarra suene mas
    // fuerte ahi en ese instante.
    void computeVerdicts (int slot, int group,
                          std::array<BandVerdict, kNumBands>& out) const
    {
        const juce::ScopedLock sl (lock);
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        const juce::uint32 staleMs = 2000;

        for (int b = 0; b < kNumBands; ++b)
        {
            float totalEnergy = 0.0f;
            float myEnergy    = 0.0f;
            float bestWeight  = 0.0f;
            float bestEnergy  = 0.0f;
            int   bestSlot    = -1;

            for (int i = 0; i < kMaxSlots; ++i)
            {
                const auto& s = slots[(size_t) i];
                if (! s.active || s.group != group) continue;
                if (now - s.lastUpdateMs > staleMs) continue;

                const float e = s.bandEnergy[(size_t) b];
                totalEnergy += e;

                // prioridad en dB -> factor lineal de energia
                const float pw = std::pow (10.0f, s.priorityDb / 10.0f);
                const float w  = e * pw;

                if (w > bestWeight) { bestWeight = w; bestEnergy = e; bestSlot = i; }
                if (i == slot) { myEnergy = e; }
            }

            BandVerdict v;

            if (totalEnergy > 1.0e-12f)
            {
                v.share       = myEnergy / totalEnergy;
                v.leaderShare = bestEnergy / totalEnergy;

                for (int i = 0; i < kMaxSlots; ++i)
                {
                    const auto& s = slots[(size_t) i];
                    if (! s.active || s.group != group) continue;
                    if (now - s.lastUpdateMs > staleMs) continue;
                    if ((s.bandEnergy[(size_t) b] / totalEnergy) > 0.15f)
                        v.competitors++;
                }
            }

            v.amDominant = (bestSlot == slot) || (bestSlot < 0);

            out[(size_t) b] = v;
        }
    }

private:
    struct Slot
    {
        bool active = false;
        int group = 0;
        float priorityDb = 0.0f;
        std::array<float, kNumBands> bandEnergy {};
        juce::uint32 lastUpdateMs = 0;
    };

    mutable juce::CriticalSection lock;
    std::array<Slot, kMaxSlots> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CarveRegistry)
};

} // namespace encaje
