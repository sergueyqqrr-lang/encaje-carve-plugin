#pragma once

#include <JuceHeader.h>
#include "CarveRegistry.h"
#include <array>
#include <atomic>
#include <vector>

class EncajeCarveAudioProcessor : public juce::AudioProcessor
{
public:
    EncajeCarveAudioProcessor();
    ~EncajeCarveAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Datos en vivo que lee el editor (nivel por banda y cuanto se esta
    // recortando en cada una ahora mismo), sin bloquear el hilo de audio.
    std::array<std::atomic<float>, encaje::kNumBands> uiBandLevel;
    std::array<std::atomic<float>, encaje::kNumBands> uiBandCutDb;
    std::array<std::atomic<int>,   encaje::kNumBands> uiCompetitors;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateFilterCoefficients (int bandIndex, float targetGainDb);

    juce::SharedResourcePointer<encaje::CarveRegistry> registry;
    int slotId = -1;
    int lastGroup = -1;

    double sr = 44100.0;

    // FFT grande (8192 muestras, ~5.4 Hz de resolucion a 44.1 kHz) con
    // ventanas solapadas al 75% (hop de 2048 muestras) para no perder
    // capacidad de reaccion: sigue actualizando cada ~46 ms, pero con 4x
    // mas resolucion de frecuencia que un bloque simple sin solape.
    static constexpr int fftOrder = 13;          // 8192 muestras
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 4; // 2048 muestras = 75% solape

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize,
                                                  juce::dsp::WindowingFunction<float>::hann };

    std::vector<float> ring;      // buffer circular con las ultimas fftSize muestras
    int writeIndex = 0;
    int hopCounter = 0;
    std::vector<float> fftData;

    std::array<float, encaje::kNumBands> bandEnergySmoothed {};

    struct BandFilter
    {
        juce::dsp::IIR::Filter<float> filterL, filterR;
        float currentGainDb = 0.0f;
    };
    std::array<BandFilter, encaje::kNumBands> bandFilters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EncajeCarveAudioProcessor)
};
