#pragma once
#include <array>
#include <cmath>
#include <algorithm>

// Reconocimiento del tipo de fuente + tabla de derechos por banda.
//
// La idea: no decidir quien manda por quien suena mas fuerte (eso no es
// criterio de mezcla), sino por QUE ES cada sonido. Un bombo tiene derecho al
// sub aunque en ese instante la voz este mas fuerte; una voz tiene derecho a
// los medios aunque la guitarra pegue mas duro ahi.

namespace encaje
{

enum class Role
{
    Kick = 0,      // grave + percusivo
    Bass,          // grave + sostenido
    SnarePerc,     // medio-alto + percusivo + ruidoso
    Lead,          // medios, tonal, presencia (voz / lead)
    Harmony,       // medios-bajos, tonal, sostenido (guitarra / teclas)
    HatsAir,       // agudo + ruidoso
    Broad,         // repartido, sin zona clara (pad, bus, ambiente)
    Count
};

inline const char* roleName (Role r)
{
    switch (r)
    {
        case Role::Kick:      return "Bombo";
        case Role::Bass:      return "Bajo";
        case Role::SnarePerc: return "Caja / Perc";
        case Role::Lead:      return "Voz / Lead";
        case Role::Harmony:   return "Armonia";
        case Role::HatsAir:   return "Hats / Aire";
        case Role::Broad:     return "Amplio";
        default:              return "?";
    }
}

// Derecho de cada rol sobre cada banda (0..1), segun convencion de mezcla.
// Sub Graves MedBajo Medios MedAlto Agudos
inline const std::array<float, 6>& roleClaim (Role r)
{
    static const std::array<std::array<float, 6>, (size_t) Role::Count> table = { {
        /* Kick      */ { 0.95f, 0.88f, 0.35f, 0.15f, 0.20f, 0.10f },
        /* Bass      */ { 0.82f, 0.95f, 0.70f, 0.25f, 0.10f, 0.05f },
        /* SnarePerc */ { 0.10f, 0.30f, 0.55f, 0.60f, 0.85f, 0.55f },
        /* Lead      */ { 0.05f, 0.25f, 0.60f, 0.95f, 0.92f, 0.60f },
        /* Harmony   */ { 0.05f, 0.30f, 0.72f, 0.62f, 0.58f, 0.45f },
        /* HatsAir   */ { 0.02f, 0.05f, 0.15f, 0.30f, 0.62f, 0.95f },
        /* Broad     */ { 0.15f, 0.42f, 0.55f, 0.52f, 0.50f, 0.52f },
    } };
    return table[(size_t) r];
}

// Rasgos que se miden del audio para decidir el rol.
struct Features
{
    float lowRatio    = 0.0f; // cuanta de su energia vive en sub+graves (0..1)
    float highRatio   = 0.0f; // cuanta vive en med-alto+agudos (0..1)
    float centroidHz  = 0.0f; // "brillo": donde esta el centro de gravedad
    float flatness    = 0.0f; // 0 = tonal (notas), 1 = ruidoso (ruido/platillos)
    float flux        = 0.0f; // 0 = sostenido, alto = golpes/transitorios
};

// Clasificador heuristico. No es reconocimiento de instrumentos por IA:
// se apoya en rasgos acusticos que separan bien las familias gruesas.
// Distinguir voz de guitarra por estos rasgos es lo mas fragil, por eso el
// plugin permite forzar el rol a mano cuando se equivoca.
inline Role classify (const Features& f)
{
    const bool percussive = f.flux > 0.35f;
    const bool noisy      = f.flatness > 0.32f;

    // Familia grave
    if (f.lowRatio > 0.55f)
        return percussive ? Role::Kick : Role::Bass;

    // Familia aguda / ruidosa
    if (f.centroidHz > 5500.0f && noisy)
        return Role::HatsAir;

    // Golpes de cuerpo medio-alto y ruidosos: caja, claps, percusion
    if (percussive && noisy && f.centroidHz > 900.0f)
        return Role::SnarePerc;

    // Familia media tonal: separar protagonista de acompanamiento.
    // Un lead tiende a vivir mas arriba (presencia) que una guitarra o teclado
    // de relleno, que carga mas el medio-bajo.
    if (f.centroidHz > 300.0f && f.centroidHz < 6000.0f && ! noisy)
        return (f.highRatio > 0.28f && f.centroidHz > 1100.0f) ? Role::Lead
                                                                : Role::Harmony;

    return Role::Broad;
}

} // namespace encaje
