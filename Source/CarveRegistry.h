#pragma once
#include <JuceHeader.h>
#include "SourceClassifier.h"
#include <array>

// Registro compartido entre todas las instancias del plugin en el mismo
// proceso de la DAW. Cada pista publica su rol detectado y su presencia por
// banda; el registro decide, banda por banda, QUIEN tiene derecho a mandar.

namespace encaje
{

constexpr int kMaxSlots = 64;

struct BandInfo { float lo, hi; const char* name; };

static const std::array<BandInfo, 6> kBands { {
    { 20.0f,   60.0f,   "Sub" },
    { 60.0f,   250.0f,  "Graves" },
    { 250.0f,  500.0f,  "Med-bajo" },
    { 500.0f,  2000.0f, "Medios" },
    { 2000.0f, 6000.0f, "Med-alto" },
    { 6000.0f, 16000.0f,"Agudos" }
} };

constexpr int kNumBands = 6;

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

struct BandVerdict
{
    float share       = 0.0f;
    float leaderShare = 0.0f;
    float myClaim     = 0.0f;
    float leaderClaim = 0.0f;
    int   competitors = 0;
    bool  amDominant  = false;
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
            auto& s = slots[(size_t) i];
            if (! s.active)
            {
                s.active = true;
                s.group = group;
                s.role = Role::Broad;
                s.priorityDb = 0.0f;
                s.bandEnergy.fill (0.0f);
                s.lastUpdateMs = juce::Time::getMillisecondCounter();
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

    void publish (int slot, int group, Role role, float priorityDb,
                  const std::array<float, kNumBands>& bandEnergy)
    {
        if (slot < 0) return;
        const juce::ScopedLock sl (lock);
        auto& s = slots[(size_t) slot];
        s.group = group;
        s.role = role;
        s.priorityDb = priorityDb;
        s.bandEnergy = bandEnergy;
        s.lastUpdateMs = juce::Time::getMillisecondCounter();
    }

    // Cuantas instancias vivas hay en el grupo (incluida la propia).
    // Sirve para que el usuario vea si las pistas se estan "viendo" entre si.
    int countActive (int group) const
    {
        const juce::ScopedLock sl (lock);
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        int n = 0;
        for (const auto& s : slots)
            if (s.active && s.group == group && now - s.lastUpdateMs <= 3000)
                ++n;
        return n;
    }

    // Decide el dueno de cada banda.
    //
    // El derecho base sale del ROL detectado (un bombo tiene derecho al sub,
    // una voz a los medios), modulado por cuanta presencia real tiene la pista
    // ahi en ese instante. La prioridad manual solo inclina la balanza.
    void computeVerdicts (int slot, int group,
                          std::array<BandVerdict, kNumBands>& out) const
    {
        const juce::ScopedLock sl (lock);
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        const juce::uint32 staleMs = 3000;

        for (int b = 0; b < kNumBands; ++b)
        {
            float total = 0.0f;
            for (const auto& s : slots)
                if (s.active && s.group == group && now - s.lastUpdateMs <= staleMs)
                    total += s.bandEnergy[(size_t) b];

            BandVerdict v;
            if (total <= 1.0e-12f) { out[(size_t) b] = v; continue; }

            int   bestSlot = -1;
            float bestScore = 0.0f;

            for (int i = 0; i < kMaxSlots; ++i)
            {
                const auto& s = slots[(size_t) i];
                if (! s.active || s.group != group) continue;
                if (now - s.lastUpdateMs > staleMs) continue;

                const float share = s.bandEnergy[(size_t) b] / total;
                const float claim = effectiveClaim (s, b);
                const float score = claim * (0.35f + 0.65f * share);

                if (share > 0.15f) v.competitors++;

                if (score > bestScore)
                {
                    bestScore = score;
                    bestSlot = i;
                    v.leaderShare = share;
                    v.leaderClaim = claim;
                }

                if (i == slot)
                {
                    v.share   = share;
                    v.myClaim = claim;
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
        Role role = Role::Broad;
        float priorityDb = 0.0f;
        std::array<float, kNumBands> bandEnergy {};
        juce::uint32 lastUpdateMs = 0;
    };

    static float effectiveClaim (const Slot& s, int band)
    {
        // +12 dB de prioridad = +0.5 de derecho; permite forzar que una pista
        // se quede una banda aunque su rol no la reclame por defecto.
        float c = roleClaim (s.role)[(size_t) band] + s.priorityDb / 24.0f;
        return juce::jlimit (0.0f, 1.5f, c);
    }

    mutable juce::CriticalSection lock;
    std::array<Slot, kMaxSlots> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CarveRegistry)
};

} // namespace encaje
