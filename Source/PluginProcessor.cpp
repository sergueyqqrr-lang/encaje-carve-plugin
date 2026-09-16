#include "PluginProcessor.h"
#include "PluginEditor.h"

EncajeCarveAudioProcessor::EncajeCarveAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    for (auto& v : uiBandLevel)   v = 0.0f;
    for (auto& v : uiBandCutDb)   v = 0.0f;
    for (auto& v : uiCompetitors) v = 0;
    for (auto& v : uiAmDominant)  v = false;
}

EncajeCarveAudioProcessor::~EncajeCarveAudioProcessor()
{
    registry->releaseSlot (slotId);
}

juce::AudioProcessorValueTreeState::ParameterLayout EncajeCarveAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "carve", 1 }, "Carve Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f));

    // Prioridad: define QUIEN manda cuando dos pistas pelean la misma banda.
    // Se suma a la energia medida, asi que una pista marcada como protagonista
    // gana la banda aunque en ese instante suene mas bajo que la otra.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "priority", 1 }, "Prioridad",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.5f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "maxcut", 1 }, "Recorte max",
        juce::NormalisableRange<float> (0.0f, 12.0f, 0.5f), 6.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "group", 1 }, "Grupo",
        juce::StringArray { "A", "B", "C", "D", "E", "F", "G", "H" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "enable", 1 }, "Carving activo", true));

    return layout;
}

void EncajeCarveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    ring.assign ((size_t) fftSize, 0.0f);
    writeIndex = 0;
    hopCounter = 0;
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    bandEnergySmoothed.fill (0.0f);

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

void EncajeCarveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    int group = (int) apvts.getRawParameterValue ("group")->load();
    if (group != lastGroup)
    {
        registry->releaseSlot (slotId);
        slotId = registry->acquireSlot (group);
        lastGroup = group;
    }

    const float carveAmount = apvts.getRawParameterValue ("carve")->load() / 100.0f;
    const float priorityDb  = apvts.getRawParameterValue ("priority")->load();
    const float maxCutDb    = apvts.getRawParameterValue ("maxcut")->load();
    const bool  enabled     = apvts.getRawParameterValue ("enable")->load() > 0.5f;

    auto* left  = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : left;

    // --- analisis: acumular una mezcla mono en un buffer circular y correr
    // la FFT (solapada al 75%) cada vez que avanzamos un hop completo ---
    for (int i = 0; i < numSamples; ++i)
    {
        float mono = numChannels > 1 ? 0.5f * (left[i] + right[i]) : left[i];
        ring[(size_t) writeIndex] = mono;
        writeIndex = (writeIndex + 1) % fftSize;
        ++hopCounter;

        if (hopCounter >= hopSize)
        {
            hopCounter = 0;

            // linearizar el buffer circular en orden cronologico: writeIndex
            // apunta justo a la muestra mas vieja (la proxima que se va a
            // sobrescribir), asi que empezamos a leer desde ahi.
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            for (int k = 0; k < fftSize; ++k)
            {
                int idx = (writeIndex + k) % fftSize;
                fftData[(size_t) k] = ring[(size_t) idx];
            }
            window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data());

            std::array<float, encaje::kNumBands> bandEnergyRaw {};
            std::array<int,   encaje::kNumBands> bandBinCount {};
            bandEnergyRaw.fill (0.0f);
            bandBinCount.fill (0);

            for (int bin = 1; bin < fftSize / 2; ++bin)
            {
                const float freq = (float) bin * (float) sr / (float) fftSize;
                const float mag  = fftData[(size_t) bin];
                const float e    = mag * mag;

                for (int b = 0; b < encaje::kNumBands; ++b)
                {
                    if (freq >= encaje::kBands[(size_t) b].lo && freq < encaje::kBands[(size_t) b].hi)
                    {
                        bandEnergyRaw[(size_t) b] += e;
                        bandBinCount[(size_t) b] += 1;
                        break;
                    }
                }
            }

            // Energia PROMEDIO por bin (densidad espectral), no la suma total.
            // Si no hicieramos esto, una banda ancha como Agudos (10 000 Hz de
            // ancho) siempre "ganaria" frente a Sub (40 Hz de ancho) solo por
            // tener muchisimos mas bins sumando, sin importar que tan fuerte
            // suene realmente cada una. Promediar hace la comparacion justa.
            for (int b = 0; b < encaje::kNumBands; ++b)
                bandEnergyRaw[(size_t) b] /= (float) juce::jmax (1, bandBinCount[(size_t) b]);

            for (int b = 0; b < encaje::kNumBands; ++b)
                bandEnergySmoothed[(size_t) b] = bandEnergySmoothed[(size_t) b] * 0.7f
                                                  + bandEnergyRaw[(size_t) b] * 0.3f;

            registry->publish (slotId, group, priorityDb, bandEnergySmoothed);
        }
    }

    // --- decidir cuanto recortar cada banda ---
    // Regla: en cada banda hay un dueno (el de mayor energia x prioridad).
    // Ese NO se toca nunca. Los demas se apartan en proporcion a que tan
    // cerca le estan pisando los talones: si ocupas casi tanto como el, le
    // estorbas de verdad y te apartas a fondo; si apenas asomas, no molestas.
    std::array<encaje::BandVerdict, encaje::kNumBands> verdicts {};
    registry->computeVerdicts (slotId, group, verdicts);

    for (int b = 0; b < encaje::kNumBands; ++b)
    {
        const auto& v = verdicts[(size_t) b];
        float targetDb = 0.0f;

        const bool contested   = v.competitors >= 2;
        const bool iAmIntruder = ! v.amDominant && v.share >= 0.12f;

        if (enabled && contested && iAmIntruder)
        {
            float ratio = v.leaderShare > 1.0e-6f ? (v.share / v.leaderShare) : 0.0f;
            ratio = juce::jlimit (0.0f, 1.0f, ratio);
            targetDb = -maxCutDb * ratio * carveAmount;
        }

        auto& bf = bandFilters[(size_t) b];
        bf.currentGainDb += (targetDb - bf.currentGainDb) * 0.15f;
        updateFilterCoefficients (b, bf.currentGainDb);

        uiBandLevel[(size_t) b].store (bandEnergySmoothed[(size_t) b]);
        uiBandCutDb[(size_t) b].store (bf.currentGainDb);
        uiCompetitors[(size_t) b].store (v.competitors);
        uiAmDominant[(size_t) b].store (contested && v.amDominant);
    }

    // --- aplicar la cadena de 6 filtros a cada canal ---
    for (int i = 0; i < numSamples; ++i)
    {
        float l = left[i];
        float r = right[i];

        for (int b = 0; b < encaje::kNumBands; ++b)
        {
            l = bandFilters[(size_t) b].filterL.processSample (l);
            r = bandFilters[(size_t) b].filterR.processSample (r);
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
