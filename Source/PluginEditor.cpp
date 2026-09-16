#include "PluginEditor.h"

EncajeCarveAudioProcessorEditor::EncajeCarveAudioProcessorEditor (EncajeCarveAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&laf);

    carveSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    carveSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    addAndMakeVisible (carveSlider);
    carveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "carve", carveSlider);

    carveLabel.setText ("Carve", juce::dontSendNotification);
    carveLabel.setJustificationType (juce::Justification::centred);
    carveLabel.setFont (juce::Font (13.0f, juce::Font::plain));
    addAndMakeVisible (carveLabel);

    prioritySlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    prioritySlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    prioritySlider.setTextValueSuffix (" dB");
    addAndMakeVisible (prioritySlider);
    priorityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "priority", prioritySlider);

    priorityLabel.setText ("Prioridad", juce::dontSendNotification);
    priorityLabel.setJustificationType (juce::Justification::centred);
    priorityLabel.setFont (juce::Font (13.0f, juce::Font::plain));
    addAndMakeVisible (priorityLabel);

    maxCutSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    maxCutSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    maxCutSlider.setTextValueSuffix (" dB");
    addAndMakeVisible (maxCutSlider);
    maxCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "maxcut", maxCutSlider);

    maxCutLabel.setText ("Recorte max", juce::dontSendNotification);
    maxCutLabel.setJustificationType (juce::Justification::centred);
    maxCutLabel.setFont (juce::Font (13.0f, juce::Font::plain));
    addAndMakeVisible (maxCutLabel);

    groupBox.addItemList ({ "A", "B", "C", "D", "E", "F", "G", "H" }, 1);
    addAndMakeVisible (groupBox);
    groupAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "group", groupBox);

    groupLabel.setText ("Grupo", juce::dontSendNotification);
    groupLabel.setJustificationType (juce::Justification::centredLeft);
    groupLabel.setFont (juce::Font (13.0f, juce::Font::plain));
    addAndMakeVisible (groupLabel);

    enableButton.setButtonText ("Carving activo");
    addAndMakeVisible (enableButton);
    enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "enable", enableButton);

    setSize (480, 440);
    startTimerHz (30);
}

EncajeCarveAudioProcessorEditor::~EncajeCarveAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void EncajeCarveAudioProcessorEditor::timerCallback()
{
    repaint();
}

void EncajeCarveAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1b1f));

    auto area = getLocalBounds().reduced (18);

    auto header = area.removeFromTop (46);
    g.setColour (juce::Colour (0xffe3b23c));
    g.setFont (juce::Font (12.5f, juce::Font::plain));
    g.drawText ("ANALISIS ESPECTRAL EN VIVO", header.removeFromTop (16),
                juce::Justification::left);
    g.setColour (juce::Colour (0xffece7dc));
    g.setFont (juce::Font (20.0f, juce::Font::bold));
    g.drawText ("Encaje Carve", header, juce::Justification::left);

    area.removeFromTop (10);
    auto meterArea = area.removeFromTop (200);
    drawBandMeters (g, meterArea);
}

void EncajeCarveAudioProcessorEditor::drawBandMeters (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int numBands = encaje::kNumBands;
    const float gap = 10.0f;
    const float bw = ((float) area.getWidth() - gap * (numBands - 1)) / (float) numBands;

    // referencia visual: banda con mas energia entre las 6 marca el 100%
    float maxLevel = 1.0e-6f;
    for (int b = 0; b < numBands; ++b)
        maxLevel = juce::jmax (maxLevel, processor.uiBandLevel[(size_t) b].load());

    for (int b = 0; b < numBands; ++b)
    {
        auto col = area.getX() + (int) (b * (bw + gap));
        juce::Rectangle<float> slot ((float) col, (float) area.getY(), bw, (float) area.getHeight() - 34.0f);

        g.setColour (juce::Colour (0xff26242c));
        g.fillRoundedRectangle (slot, 3.0f);

        float level = processor.uiBandLevel[(size_t) b].load();
        float norm  = juce::jlimit (0.0f, 1.0f, std::sqrt (level / maxLevel));
        auto fillH  = slot.getHeight() * norm;
        juce::Rectangle<float> fillRect (slot.getX(), slot.getBottom() - fillH, slot.getWidth(), fillH);

        int competitors = processor.uiCompetitors[(size_t) b].load();
        float cutDb = processor.uiBandCutDb[(size_t) b].load();
        bool amDominant = processor.uiAmDominant[(size_t) b].load();
        bool beingCut = cutDb < -0.3f;

        // verde = esta banda es mia, mando yo aqui
        // rojo  = me estoy apartando para dejarle espacio a otra pista
        // neutro = no hay disputa
        juce::Colour fillCol = amDominant ? juce::Colour (0xff6fa98a)
                             : beingCut   ? juce::Colour (0xffc1443c)
                                          : juce::Colour (0xff7f7a70);

        g.setColour (fillCol.withAlpha (0.9f));
        g.fillRoundedRectangle (fillRect, 2.0f);

        if (amDominant)
        {
            g.setColour (juce::Colour (0xff6fa98a));
            g.drawRoundedRectangle (slot, 3.0f, 1.4f);
        }
        else if (beingCut)
        {
            g.setColour (juce::Colour (0xffc1443c));
            g.drawRoundedRectangle (slot, 3.0f, 1.4f);
        }

        juce::ignoreUnused (competitors);

        g.setColour (juce::Colour (0xff9c9689));
        g.setFont (juce::Font (10.5f, juce::Font::plain));
        g.drawText (encaje::kBands[(size_t) b].name,
                    juce::Rectangle<int> (col, area.getBottom() - 32, (int) bw, 14),
                    juce::Justification::centred);

        juce::String cutText = cutDb < -0.3f ? (juce::String (cutDb, 1) + " dB") : juce::String ("-");
        g.setColour (cutDb < -0.3f ? juce::Colour (0xffc1443c) : juce::Colour (0xff5f5a51));
        g.setFont (juce::Font (10.5f, juce::Font::plain));
        g.drawText (cutText,
                    juce::Rectangle<int> (col, area.getBottom() - 18, (int) bw, 14),
                    juce::Justification::centred);
    }
}

void EncajeCarveAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (18);
    area.removeFromTop (46 + 10 + 200 + 14);

    // fila de tres perillas
    auto knobRow = area.removeFromTop (110);
    auto third = knobRow.getWidth() / 3;

    auto carveArea = knobRow.removeFromLeft (third).reduced (4);
    carveLabel.setBounds (carveArea.removeFromBottom (16));
    carveSlider.setBounds (carveArea);

    auto priorityArea = knobRow.removeFromLeft (third).reduced (4);
    priorityLabel.setBounds (priorityArea.removeFromBottom (16));
    prioritySlider.setBounds (priorityArea);

    auto maxCutArea = knobRow.reduced (4);
    maxCutLabel.setBounds (maxCutArea.removeFromBottom (16));
    maxCutSlider.setBounds (maxCutArea);

    // fila inferior: grupo + activar
    area.removeFromTop (10);
    auto bottomRow = area.removeFromTop (28);
    auto leftHalf = bottomRow.removeFromLeft (bottomRow.getWidth() / 2).reduced (4, 0);
    groupLabel.setBounds (leftHalf.removeFromLeft (55));
    groupBox.setBounds (leftHalf);
    enableButton.setBounds (bottomRow.reduced (8, 0));
}
