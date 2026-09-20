#pragma once
#include <JuceHeader.h>
#include "SourceClassifier.h"
#include <array>
#include <algorithm>

// Registro compartido entre TODAS las instancias del plugin, sin importar si
// la DAW las corre en el mismo proceso o cada una en su propio proceso
// aislado (Studio One puede hacer esto para estabilidad). Por eso esto ya NO
// es un singleton en memoria del programa: es un archivo mapeado en memoria,
// visible para cualquier proceso de la maquina, protegido con un lock de
// sistema operativo. Si el plugin corriera en procesos separados y este
// registro fuera solo memoria interna, cada instancia se veria completamente
// sola y el plugin no haria nunca nada, sin importar los ajustes.

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

// Todo lo que una pista necesita saber de su grupo en un solo golpe: el
// veredicto de cada banda, cuantas pistas hay activas, y el ajuste de
// volumen general que le toca segun su lugar en el orden de prioridad.
struct GroupStatus
{
    std::array<BandVerdict, kNumBands> bands;
    int   peerCount     = 1;   // incluye a la pista misma
    int   priorityRank  = 0;   // 0 = el de mayor prioridad
    int   priorityTiers = 1;   // cuantos niveles de prioridad distintos hay
};

#pragma pack(push, 1)
struct SharedSlot
{
    juce::int32 active = 0;
    juce::int32 group = 0;
    juce::int32 role = 0;
    float priorityDb = 0.0f;
    float bandEnergy[kNumBands] = { 0,0,0,0,0,0 };
    juce::uint32 lastUpdateMs = 0;
};

struct SharedTable
{
    juce::int32 magic = 0;
    SharedSlot slots[kMaxSlots];
    // Quien fue el dueno de cada banda, por grupo, la ultima vez. Se usa
    // para exigirle a un retador que gane con margen claro antes de
    // arrebatarle el puesto al dueno actual (histeresis contra el parpadeo).
    juce::int32 lastOwner[8][kNumBands];
};
#pragma pack(pop)

class CarveRegistry
{
public:
    CarveRegistry()
        : ipLock ("EncajeCarve_IPLock_v1")
    {
        openOrCreateSharedFile();
    }

    int acquireSlot (int group)
    {
        if (table == nullptr) return -1;
        const juce::InterProcessLock::ScopedLockType sl (ipLock);
        const juce::uint32 now = juce::Time::getMillisecondCounter();

        for (int i = 0; i < kMaxSlots; ++i)
        {
            auto& s = table->slots[i];
            // libre, o abandonado hace mas de 10s por una sesion anterior
            const bool stale = s.active != 0 && (now - s.lastUpdateMs > 10000);
            if (s.active == 0 || stale)
            {
                s.active = 1;
                s.group = group;
                s.role = (juce::int32) Role::Broad;
                s.priorityDb = 0.0f;
                for (auto& e : s.bandEnergy) e = 0.0f;
                s.lastUpdateMs = now;
                return i;
            }
        }
        return -1;
    }

    void releaseSlot (int slot)
    {
        if (table == nullptr || slot < 0) return;
        const juce::InterProcessLock::ScopedLockType sl (ipLock);
        table->slots[(size_t) slot].active = 0;
    }

    void publish (int slot, int group, Role role, float priorityDb,
                  const std::array<float, kNumBands>& bandEnergy)
    {
        if (table == nullptr || slot < 0) return;
        const juce::InterProcessLock::ScopedLockType sl (ipLock);
        auto& s = table->slots[(size_t) slot];
        s.group = group;
        s.role = (juce::int32) role;
        s.priorityDb = priorityDb;
        for (int i = 0; i < kNumBands; ++i) s.bandEnergy[i] = bandEnergy[(size_t) i];
        s.lastUpdateMs = juce::Time::getMillisecondCounter();
    }

    // Un solo golpe de lock: veredictos de banda + conteo de pares + rango
    // de prioridad. Evita tomar el lock varias veces por bloque de audio.
    void computeStatus (int slot, int group, GroupStatus& out) const
    {
        out = GroupStatus {};
        if (table == nullptr) return;

        const juce::InterProcessLock::ScopedLockType sl (ipLock);
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        const juce::uint32 staleMs = 3000;

        // --- pares activos + niveles de prioridad presentes ---
        int peerCount = 0;
        float myPriority = 0.0f;
        std::array<float, kMaxSlots> tierValues {};
        int nTierValues = 0;

        for (int i = 0; i < kMaxSlots; ++i)
        {
            const auto& s = table->slots[(size_t) i];
            if (s.active == 0 || s.group != group) continue;
            if (now - s.lastUpdateMs > staleMs) continue;

            ++peerCount;
            tierValues[(size_t) nTierValues++] = s.priorityDb;
            if (i == slot) myPriority = s.priorityDb;
        }

        std::sort (tierValues.begin(), tierValues.begin() + nTierValues, std::greater<float>());
        int nTiers = 0;
        for (int i = 0; i < nTierValues; ++i)
        {
            if (i == 0 || std::abs (tierValues[(size_t) i] - tierValues[(size_t) (i - 1)]) > 0.01f)
                tierValues[(size_t) nTiers++] = tierValues[(size_t) i];
        }

        int rank = 0;
        for (int i = 0; i < nTiers; ++i)
            if (std::abs (tierValues[(size_t) i] - myPriority) < 0.01f) { rank = i; break; }

        out.peerCount     = juce::jmax (1, peerCount);
        out.priorityRank  = rank;
        out.priorityTiers = juce::jmax (1, nTiers);

        // --- veredictos por banda ---
        for (int b = 0; b < kNumBands; ++b)
        {
            float total = 0.0f;
            for (const auto& s : table->slots)
                if (s.active != 0 && s.group == group && now - s.lastUpdateMs <= staleMs)
                    total += s.bandEnergy[b];

            BandVerdict v;
            if (total <= 1.0e-12f) { out.bands[(size_t) b] = v; continue; }

            // Se guarda el share, el claim y el score de cada pista activa
            // para poder recuperar los del dueno anterior sin recorrer todo
            // otra vez.
            std::array<float, kMaxSlots> shareOf {};
            std::array<float, kMaxSlots> claimOf {};
            std::array<float, kMaxSlots> scoreOf {};
            shareOf.fill (0.0f); claimOf.fill (0.0f); scoreOf.fill (0.0f);

            int   challengerSlot  = -1;
            float challengerScore = 0.0f;

            for (int i = 0; i < kMaxSlots; ++i)
            {
                const auto& s = table->slots[(size_t) i];
                if (s.active == 0 || s.group != group) continue;
                if (now - s.lastUpdateMs > staleMs) continue;

                const float share = s.bandEnergy[b] / total;
                const float claim = effectiveClaim (s, b);
                const float score = claim * (0.35f + 0.65f * share);

                shareOf[(size_t) i] = share;
                claimOf[(size_t) i] = claim;
                scoreOf[(size_t) i] = score;

                // OJO: contar por CLAIM (que tan propio de su tipo de sonido
                // es esta banda), no por el volumen actual. Si se contara por
                // volumen, un sonido enterrado (que es justo el que se
                // necesita subir) nunca calificaria como "competidor" por
                // tener poca energia ahora mismo, y jamas se le aplicaria el
                // ajuste que lo saque del fondo de la mezcla.
                if (claim >= 0.15f) v.competitors++;

                if (score > challengerScore) { challengerScore = score; challengerSlot = i; }
                if (i == slot) v.share = share;
            }

            // --- histeresis: el dueno anterior conserva el puesto salvo que
            // el retador lo supere con margen claro. Sin esto, el dueno de
            // una banda puede cambiar decenas de veces por segundo cada vez
            // que, por ejemplo, un kick decae por debajo de un bajo
            // sostenido, y cada cambio de dueno es un salto brusco de
            // ganancia (el "sube y baja" inestable). ---
            const float hysteresisMargin = 0.20f; // el retador debe ganar por 20%
            int prevOwner = table->lastOwner[(size_t) group][(size_t) b];
            bool prevOwnerStillValid = prevOwner >= 0
                                        && table->slots[(size_t) prevOwner].active != 0
                                        && table->slots[(size_t) prevOwner].group == group
                                        && now - table->slots[(size_t) prevOwner].lastUpdateMs <= staleMs;

            int finalOwner;
            if (! prevOwnerStillValid || challengerSlot < 0)
                finalOwner = challengerSlot;
            else if (challengerScore > scoreOf[(size_t) prevOwner] * (1.0f + hysteresisMargin))
                finalOwner = challengerSlot; // gano con margen claro: se lleva el puesto
            else
                finalOwner = prevOwner;      // se queda el dueno de antes

            table->lastOwner[(size_t) group][(size_t) b] = finalOwner;

            v.leaderShare = finalOwner >= 0 ? shareOf[(size_t) finalOwner] : 0.0f;
            v.leaderClaim = finalOwner >= 0 ? claimOf[(size_t) finalOwner] : 0.0f;
            v.myClaim     = claimOf[(size_t) slot];
            v.amDominant  = (finalOwner == slot) || (finalOwner < 0);

            out.bands[(size_t) b] = v;
        }
    }

private:
    static float effectiveClaim (const SharedSlot& s, int band)
    {
        float c = roleClaim ((Role) s.role)[(size_t) band] + s.priorityDb / 24.0f;
        return juce::jlimit (0.0f, 1.5f, c);
    }

    void openOrCreateSharedFile()
    {
        const juce::InterProcessLock::ScopedLockType sl (ipLock);

        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("EncajeCarveShared_v1.dat");

        const juce::int64 neededSize = (juce::int64) sizeof (SharedTable);

        if (! file.existsAsFile() || file.getSize() != neededSize)
        {
            file.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
            {
                os->setPosition (neededSize - 1);
                os->writeByte (0);
                os->flush();
            }
        }

        mmf = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readWrite);
        if (mmf->getData() != nullptr && mmf->getSize() >= (size_t) neededSize)
        {
            table = static_cast<SharedTable*> (mmf->getData());
            if (table->magic != 0x45434152) // 'ECAR'
            {
                // primera vez que se crea el archivo: limpiar todo
                std::memset (table, 0, sizeof (SharedTable));
                table->magic = 0x45434152;
                for (auto& row : table->lastOwner)
                    for (auto& v : row)
                        v = -1;
            }
        }
    }

    mutable juce::InterProcessLock ipLock;
    std::unique_ptr<juce::MemoryMappedFile> mmf;
    SharedTable* table = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CarveRegistry)
};

} // namespace encaje
