#include "PluginProcessor.h"
#include "PluginEditor.h"

EncajeCarveAudioProcessor::EncajeCarveAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    for (auto& v : uiBandLevel)  v = 0.0f;
    for (auto& v : uiBandCutDb)  v = 0.0f;
    for (auto& v : uiAmDominant) v = false;
    for (auto& v : uiContested)  v = false;
}

EncajeCarveAudioProcessor::~EncajeCarveAudioProcessor()
{
    registry->releaseSlot (slotId);
}

juce::AudioProcessorValueTreeState::ParameterLayout EncajeCarveAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "carve", 1 }, "Carve",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "maxcut", 1 }, "Recorte max",
        juce::NormalisableRange<float> (0.0f, 12.0f, 0.5f), 9.0f));

    // Separacion objetivo: cuantos dB deberia haber, en una banda disputada,
    // entre el dueno y todo lo demas que compite ahi. No es una ley fisica:
    // es una practica estandar de mezcla (documentada, por ejemplo, en
    // Mixing Secrets for the Small Studio de Mike Senior) para evitar que
    // dos sonidos se enmascaren entre si en la misma zona de frecuencia.
    // 6-10 dB es el punto de partida tipico; el genero y la densidad del
    // arreglo cambian lo que suena bien, por eso es ajustable.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "targetsep", 1 }, "Separacion objetivo",
        juce::NormalisableRange<float> (3.0f, 15.0f, 0.5f), 8.0f));

    // "Auto" = el plugin decide el rol escuchando. Las demas opciones fuerzan
    // el rol cuando la deteccion se equivoca.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "role", 1 }, "Tipo de sonido",
        juce::StringArray { "Auto", "Bombo", "Bajo", "Caja / Perc",
                            "Voz / Lead", "Armonia", "Hats / Aire", "Amplio" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "priority", 1 }, "Prioridad",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.5f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "group", 1 }, "Grupo",
        juce::StringArray { "A", "B", "C", "D", "E", "F", "G", "H" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "enable", 1 }, "Activo", true));

    // Deja oir SOLO lo que el plugin le esta quitando a esta pista.
    // Es la forma de comprobar con el oido que si esta actuando: si aqui se
    // escucha algo, el recorte esta ocurriendo.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "audition", 1 }, "Escuchar recorte", false));

    return layout;
}

void EncajeCarveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    ring.assign ((size_t) fftSize, 0.0f);
    writeIndex = 0;
    hopCounter = 0;
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    prevMag.assign ((size_t) fftSize / 2, 0.0f);
    havePrevMag = false;
    bandEnergySmoothed.fill (0.0f);
    feat = {};

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = 1;

    for (int b = 0; b < encaje::kNumBands; ++b)
    {
        bandFilters[(size_t) b].filterL.prepare (spec);
        bandFilters[(size_t) b].filterR.prepare (spec);
        bandFilters[(size_t) b].filterL.reset();
        bandFilters[(size_t) b].filterR.reset();
        bandFilters[(size_t) b].currentGainDb = 0.0f;
        updateFilterCoefficients (b, 0.0f);
    }

    int group = (int) apvts.getRawParameterValue ("group")->load();
    if (slotId < 0)
        slotId = registry->acquireSlot (group);
    lastGroup = group;
}

void EncajeCarveAudioProcessor::releaseResources()
{
    registry->releaseSlot (slotId);
    slotId = -1;
}

bool EncajeCarveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto mono   = juce::AudioChannelSet::mono();
    auto stereo = juce::AudioChannelSet::stereo();
    auto in  = layouts.getMainInputChannelSet();
    auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == mono || in == stereo;
}

void EncajeCarveAudioProcessor::updateFilterCoefficients (int b, float targetGainDb)
{
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        sr, encaje::bandCenterFreq (b), encaje::bandQ (b),
        juce::Decibels::decibelsToGain (targetGainDb));

    bandFilters[(size_t) b].filterL.coefficients = coeffs;
    bandFilters[(size_t) b].filterR.coefficients = coeffs;
}

// Analiza una ventana completa: saca la presencia por banda y los rasgos
// acusticos con los que se decide QUE tipo de sonido es esta pista.
void EncajeCarveAudioProcessor::analyseFrame()
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    for (int k = 0; k < fftSize; ++k)
        fftData[(size_t) k] = ring[(size_t) ((writeIndex + k) % fftSize)];

    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const int nBins = fftSize / 2;

    std::array<float, encaje::kNumBands> sumE {};   // energia total por banda
    std::array<float, encaje::kNumBands> avgE {};   // energia media por bin
    std::array<int,   encaje::kNumBands> nBinsB {};
    sumE.fill (0.0f); avgE.fill (0.0f); nBinsB.fill (0);

    float magSum = 0.0f, logMagSum = 0.0f, centroidNum = 0.0f, centroidDen = 0.0f;
    float flux = 0.0f;
    int   flatCount = 0;

    for (int bin = 1; bin < nBins; ++bin)
    {
        const float freq = (float) bin * (float) sr / (float) fftSize;
        const float mag  = fftData[(size_t) bin];
        const float e    = mag * mag;

        for (int b = 0; b < encaje::kNumBands; ++b)
        {
            if (freq >= encaje::kBands[(size_t) b].lo && freq < encaje::kBands[(size_t) b].hi)
            {
                sumE[(size_t) b] += e;
                nBinsB[(size_t) b] += 1;
                break;
            }
        }

        if (freq >= 100.0f && freq <= 16000.0f)
        {
            magSum    += mag;
            logMagSum += std::log (mag + 1.0e-9f);
            ++flatCount;
            centroidNum += freq * mag;
            centroidDen += mag;
        }

        if (havePrevMag)
            flux += juce::jmax (0.0f, mag - prevMag[(size_t) bin]);

        prevMag[(size_t) bin] = mag;
    }
    havePrevMag = true;

    for (int b = 0; b < encaje::kNumBands; ++b)
        avgE[(size_t) b] = sumE[(size_t) b] / (float) juce::jmax (1, nBinsB[(size_t) b]);

    // presencia por banda: promedio por bin, para que una banda ancha no gane
    // solo por tener mas bins que sumar
    // Suavizado mas lento de lo que parece necesario a primera vista: un
    // kick sube y baja de energia varias veces por segundo, y si el reparto
    // de bandas persigue eso en vivo, el "dueno" de una banda y el ajuste de
    // los demas laten al ritmo del kick en vez de reflejar el balance
    // sostenido de la mezcla, que es lo que realmente importa aqui.
    for (int b = 0; b < encaje::kNumBands; ++b)
        bandEnergySmoothed[(size_t) b] = bandEnergySmoothed[(size_t) b] * 0.85f
                                          + avgE[(size_t) b] * 0.15f;

    // --- rasgos (sobre energia total, que es lo perceptualmente relevante) ---
    float totalE = 0.0f;
    for (auto e : sumE) totalE += e;

    encaje::Features f;
    if (totalE > 1.0e-12f)
    {
        f.lowRatio  = (sumE[0] + sumE[1]) / totalE;
        f.highRatio = (sumE[4] + sumE[5]) / totalE;
    }
    f.centroidHz = centroidDen > 1.0e-9f ? centroidNum / centroidDen : 0.0f;

    if (flatCount > 0 && magSum > 1.0e-9f)
    {
        const float geoMean = std::exp (logMagSum / (float) flatCount);
        const float ariMean = magSum / (float) flatCount;
        f.flatness = juce::jlimit (0.0f, 1.0f, geoMean / (ariMean + 1.0e-12f));
    }
    f.flux = magSum > 1.0e-9f ? juce::jlimit (0.0f, 1.0f, flux / magSum) : 0.0f;

    // suavizado lento: el tipo de sonido no cambia de un frame a otro
    const float a = 0.05f;
    feat.lowRatio   = feat.lowRatio   * (1 - a) + f.lowRatio   * a;
    feat.highRatio  = feat.highRatio  * (1 - a) + f.highRatio  * a;
    feat.centroidHz = feat.centroidHz * (1 - a) + f.centroidHz * a;
    feat.flatness   = feat.flatness   * (1 - a) + f.flatness   * a;
    feat.flux       = feat.flux       * (1 - a) + f.flux       * a;
}

void EncajeCarveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    const int   group       = (int) apvts.getRawParameterValue ("group")->load();
    const int   roleChoice  = (int) apvts.getRawParameterValue ("role")->load();
    const float carveAmount = apvts.getRawParameterValue ("carve")->load() / 100.0f;
    const float maxCutDb    = apvts.getRawParameterValue ("maxcut")->load();
    const float targetSepDb = apvts.getRawParameterValue ("targetsep")->load();
    const float priorityDb  = apvts.getRawParameterValue ("priority")->load();
    const bool  enabled     = apvts.getRawParameterValue ("enable")->load() > 0.5f;
    const bool  audition    = apvts.getRawParameterValue ("audition")->load() > 0.5f;

    if (group != lastGroup)
    {
        registry->releaseSlot (slotId);
        slotId = registry->acquireSlot (group);
        lastGroup = group;
    }

    auto* left  = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : left;

    // --- analisis con ventanas solapadas ---
    for (int i = 0; i < numSamples; ++i)
    {
        const float mono = numChannels > 1 ? 0.5f * (left[i] + right[i]) : left[i];
        ring[(size_t) writeIndex] = mono;
        writeIndex = (writeIndex + 1) % fftSize;

        if (++hopCounter >= hopSize)
        {
            hopCounter = 0;
            analyseFrame();

            // rol: automatico por deteccion, o forzado por el usuario
            detectedRole = (roleChoice == 0) ? encaje::classify (feat)
                                              : (encaje::Role) (roleChoice - 1);

            registry->publish (slotId, group, detectedRole, priorityDb, bandEnergySmoothed);
        }
    }

    uiDetectedRole.store ((int) detectedRole);

    // --- un solo golpe de lock: veredictos de banda + orden de prioridad ---
    encaje::GroupStatus status;
    registry->computeStatus (slotId, group, status);

    uiPeersInGroup.store (status.peerCount);
    uiPriorityRank.store (status.priorityRank);
    uiPriorityTiers.store (status.priorityTiers);

    // --- 1) AJUSTE DE VOLUMEN GENERAL POR PRIORIDAD ---
    // Esto es deliberadamente simple y de banda ancha, no sutil: se agrupan
    // las pistas por su valor de Prioridad. Las que comparten el mismo valor
    // quedan EMPATADAS entre si (nunca se tocan una a otra). La de mayor
    // prioridad no se toca; cada escalon por debajo del primero baja un paso
    // mas de volumen. Si nadie ha tocado la Prioridad (todas en 0), hay un
    // solo nivel y el ajuste es 0 dB para todos: no hacer nada es lo
    // correcto hasta que el usuario decida un orden.
    // Coeficiente de suavizado calculado a partir del TIEMPO real, no del
    // numero de bloque. Un coeficiente fijo "por bloque" cambia de velocidad
    // segun el buffer que use la DAW: con un buffer chico (128 muestras,
    // muy comun en Studio One) un coeficiente de 0.07 por bloque equivale a
    // reaccionar casi instantaneamente, y cualquier fluctuacion normal del
    // audio se siente como un volumen inestable, subiendo y bajando todo el
    // tiempo. Con este calculo, el tiempo de reaccion es siempre el mismo
    // (aca, ~250 ms) sin importar el tamano de buffer.
    const float smoothTimeConstantSec = 0.25f;
    const float blockSeconds = (float) numSamples / (float) sr;
    const float smoothCoeff = 1.0f - std::exp (-blockSeconds / smoothTimeConstantSec);

    float overallTargetDb = 0.0f;
    if (enabled && status.priorityTiers > 1)
        overallTargetDb = -maxCutDb * carveAmount
                            * ((float) status.priorityRank / (float) (status.priorityTiers - 1));

    overallGainDbSmoothed += (overallTargetDb - overallGainDbSmoothed) * smoothCoeff;
    uiOverallGainDb.store (overallGainDbSmoothed);
    const float overallGainLin = juce::Decibels::decibelsToGain (overallGainDbSmoothed);

    // --- 2) RECORTE FINO POR BANDA (carving) ---
    // El dueno de la banda (mas derecho segun su tipo de sonido) no se toca.
    // Los demas se apartan segun la BRECHA DE DERECHO, con un piso para que
    // un choque real nunca quede en un recorte inaudible.
    for (int b = 0; b < encaje::kNumBands; ++b)
    {
        const auto& v = status.bands[(size_t) b];
        float targetDb = 0.0f;

        // Hay banda disputada si existe un dueno claro (leaderClaim) y hay al
        // menos otra pista viva en el grupo. OJO: no se exige que MI parte
        // supere ningun porcentaje minimo aqui a proposito -- un sonido
        // enterrado (con poca energia en esta banda) es justo el que
        // necesita que lo suban, y exigirle un minimo de presencia para
        // "calificar" era lo que le impedia recibir ese boost.
        const bool contested = v.leaderClaim >= 0.40f && status.peerCount >= 2;

        // El dueno de la banda es el punto de referencia: nunca se toca.
        // A cada uno de los demas se le empuja hacia la SEPARACION OBJETIVO
        // respecto al dueno, en la direccion que haga falta:
        //   - si ya esta mas separado de lo necesario (enterrado), se sube.
        //   - si esta invadiendo (poca separacion real), se baja.
        // Solo se toca una banda si el tipo de sonido de esta pista tiene
        // sentido ahi (myClaim >= 0.15): no se sube el "sub" de unos hats
        // solo porque este muy bajo, ya que ahi no deberia sonar nada.
        if (enabled && contested && ! v.amDominant && v.myClaim >= 0.15f)
        {
            float currentSepDb = 10.0f * std::log10 (juce::jmax (v.leaderShare, 1.0e-9f))
                                - 10.0f * std::log10 (juce::jmax (v.share, 1.0e-9f));

            float adjustment = (currentSepDb - targetSepDb) * carveAmount;

            // Tope de seguridad: nunca se sube mas de 6 dB de golpe. Un
            // sonido casi silencioso en una banda daria una separacion
            // enorme (log de un numero cercano a cero) y sin este limite se
            // podria disparar el ruido de fondo a un volumen absurdo.
            const float maxBoostDb = 6.0f;
            targetDb = juce::jlimit (-maxCutDb, maxBoostDb, adjustment);
        }

        auto& bf = bandFilters[(size_t) b];
        bf.currentGainDb += (targetDb - bf.currentGainDb) * smoothCoeff;

        if (std::abs (bf.currentGainDb - bf.appliedGainDb) > 0.05f)
        {
            updateFilterCoefficients (b, bf.currentGainDb);
            bf.appliedGainDb = bf.currentGainDb;
        }

        uiBandLevel[(size_t) b].store (bandEnergySmoothed[(size_t) b]);
        uiBandCutDb[(size_t) b].store (bf.currentGainDb);
        uiAmDominant[(size_t) b].store (contested && v.amDominant);
        uiContested[(size_t) b].store (contested);
    }

    // --- aplicar la cadena de filtros ---
    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = left[i];
        const float dryR = right[i];

        float l = dryL;
        float r = dryR;

        for (int b = 0; b < encaje::kNumBands; ++b)
        {
            l = bandFilters[(size_t) b].filterL.processSample (l);
            r = bandFilters[(size_t) b].filterR.processSample (r);
        }

        l *= overallGainLin;
        r *= overallGainLin;

        if (audition)
        {
            // Solo lo que se le quito a la señal, amplificado para poder
            // juzgarlo. Si aqui no se oye nada, el plugin no esta recortando.
            l = (dryL - l) * 4.0f;
            r = (dryR - r) * 4.0f;
        }

        left[i] = l;
        if (numChannels > 1) right[i] = r;
    }
}

juce::AudioProcessorEditor* EncajeCarveAudioProcessor::createEditor()
{
    return new EncajeCarveAudioProcessorEditor (*this);
}

void EncajeCarveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        juce::MemoryOutputStream mos (destData, true);
        state.writeToStream (mos);
    }
}

void EncajeCarveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (tree.isValid())
        apvts.replaceState (tree);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EncajeCarveAudioProcessor();
}
