#include "PluginEditor.h"

EncajeCarveAudioProcessorEditor::EncajeCarveAudioProcessorEditor (EncajeCarveAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&laf);

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& text,
                             const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 18);
        if (suffix.isNotEmpty()) s.setTextValueSuffix (suffix);
        addAndMakeVisible (s);

        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (12.0f, juce::Font::plain));
        addAndMakeVisible (l);
    };

    setupKnob (carveSlider,     carveLabel,     "Carve",              "");
    setupKnob (maxCutSlider,    maxCutLabel,    "Recorte max",        " dB");
    setupKnob (prioritySlider,  priorityLabel,  "Prioridad",          " dB");
    setupKnob (targetSepSlider, targetSepLabel, "Separacion objetivo"," dB");

    carveAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "carve", carveSlider);
    maxCutAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "maxcut", maxCutSlider);
    priorityAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "priority", prioritySlider);
    targetSepAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, "targetsep", targetSepSlider);

    roleBox.addItemList ({ "Auto", "Bombo", "Bajo", "Caja / Perc",
                           "Voz / Lead", "Armonia", "Hats / Aire", "Amplio" }, 1);
    addAndMakeVisible (roleBox);
    roleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "role", roleBox);

    roleLabel.setText ("Tipo", juce::dontSendNotification);
    roleLabel.setFont (juce::Font (12.0f, juce::Font::plain));
    addAndMakeVisible (roleLabel);

    groupBox.addItemList ({ "A", "B", "C", "D", "E", "F", "G", "H" }, 1);
    addAndMakeVisible (groupBox);
    groupAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "group", groupBox);

    groupLabel.setText ("Grupo", juce::dontSendNotification);
    groupLabel.setFont (juce::Font (12.0f, juce::Font::plain));
    addAndMakeVisible (groupLabel);

    enableButton.setButtonText ("Activo");
    addAndMakeVisible (enableButton);
    enableAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "enable", enableButton);

    auditionButton.setButtonText ("Escuchar recorte");
    addAndMakeVisible (auditionButton);
    auditionAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, "audition", auditionButton);

    setSize (520, 525);
    startTimerHz (24);
}

EncajeCarveAudioProcessorEditor::~EncajeCarveAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void EncajeCarveAudioProcessorEditor::timerCallback() { repaint(); }

void EncajeCarveAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1b1f));

    auto area = getLocalBounds().reduced (18);

    // --- cabecera: que cree que es esta pista y a cuantas otras esta viendo ---
    auto header = area.removeFromTop (58);

    const int roleIdx = processor.uiDetectedRole.load();
    const int peers   = processor.uiPeersInGroup.load();

    g.setColour (juce::Colour (0xffe3b23c));
    g.setFont (juce::Font (11.5f, juce::Font::plain));
    g.drawText ("ESCUCHANDO ESTA PISTA COMO", header.removeFromTop (14),
                juce::Justification::left);

    auto titleRow = header.removeFromTop (26);
    g.setColour (juce::Colour (0xffece7dc));
    g.setFont (juce::Font (21.0f, juce::Font::bold));
    g.drawText (encaje::roleName ((encaje::Role) roleIdx), titleRow,
                juce::Justification::left);

    // Contador de pistas que se estan viendo entre si. Si aqui dice 1 con
    // varios plugins cargados, las instancias no se estan comunicando y nada
    // de lo demas va a funcionar, sin importar los ajustes.
    juce::String peerText = peers <= 1
        ? "Sola en el grupo: no hay con quien repartir espacio"
        : ("Viendo " + juce::String (peers - 1) + " pista(s) mas en el grupo");
    g.setColour (peers <= 1 ? juce::Colour (0xffc1443c) : juce::Colour (0xff6fa98a));
    g.setFont (juce::Font (12.0f, juce::Font::plain));
    g.drawText (peerText, header, juce::Justification::left);

    area.removeFromTop (10);

    // --- ajuste de volumen general por prioridad: el numero que debe moverse
    // en cuanto pongas distinta Prioridad en dos pistas ---
    auto gainBox = area.removeFromTop (46);
    const float overallDb = processor.uiOverallGainDb.load();
    const int   rank      = processor.uiPriorityRank.load();
    const int   tiers     = processor.uiPriorityTiers.load();

    g.setColour (juce::Colour (0xff26242c));
    g.fillRoundedRectangle (gainBox.toFloat(), 4.0f);

    auto gainTextArea = gainBox.reduced (12, 6);
    g.setColour (juce::Colour (0xff9c9689));
    g.setFont (juce::Font (11.0f, juce::Font::plain));
    g.drawText ("AJUSTE DE VOLUMEN GENERAL (por prioridad)",
                gainTextArea.removeFromTop (14), juce::Justification::left);

    juce::String gainText = juce::String (overallDb, 1) + " dB";
    if (tiers <= 1)
        gainText += "   (todas en la misma prioridad: nadie se toca)";
    else
        gainText += "   (nivel " + juce::String (rank + 1) + " de " + juce::String (tiers) + ")";

    g.setColour (overallDb < -0.3f ? juce::Colour (0xffc1443c) : juce::Colour (0xff6fa98a));
    g.setFont (juce::Font (16.0f, juce::Font::bold));
    g.drawText (gainText, gainTextArea, juce::Justification::left);

    area.removeFromTop (8);
    drawBandMeters (g, area.removeFromTop (170));
}

void EncajeCarveAudioProcessorEditor::drawBandMeters (juce::Graphics& g, juce::Rectangle<int> area)
{
    const int numBands = encaje::kNumBands;
    const float gap = 9.0f;
    const float bw = ((float) area.getWidth() - gap * (numBands - 1)) / (float) numBands;

    float maxLevel = 1.0e-9f;
    for (int b = 0; b < numBands; ++b)
        maxLevel = juce::jmax (maxLevel, processor.uiBandLevel[(size_t) b].load());

    for (int b = 0; b < numBands; ++b)
    {
        const auto col = area.getX() + (int) (b * (bw + gap));
        juce::Rectangle<float> slot ((float) col, (float) area.getY(), bw,
                                      (float) area.getHeight() - 34.0f);

        g.setColour (juce::Colour (0xff26242c));
        g.fillRoundedRectangle (slot, 3.0f);

        const float level = processor.uiBandLevel[(size_t) b].load();
        const float norm  = juce::jlimit (0.0f, 1.0f, std::sqrt (level / maxLevel));
        const float fillH = slot.getHeight() * norm;
        juce::Rectangle<float> fillRect (slot.getX(), slot.getBottom() - fillH,
                                          slot.getWidth(), fillH);

        const float adjDb    = processor.uiBandCutDb[(size_t) b].load();
        const bool  mine     = processor.uiAmDominant[(size_t) b].load();
        const bool  fight    = processor.uiContested[(size_t) b].load();
        const bool  beingCut   = adjDb < -0.3f;
        const bool  beingBoost = adjDb > 0.3f;

        // verde  = esta banda es mia, aqui voy al frente
        // rojo   = me bajan (estaba invadiendo el espacio del dueno)
        // azul   = me suben (estaba enterrado por debajo de lo saludable)
        // neutro = sin ajuste
        juce::Colour fillCol = mine        ? juce::Colour (0xff6fa98a)
                             : beingCut    ? juce::Colour (0xffc1443c)
                             : beingBoost  ? juce::Colour (0xff5aa8d4)
                                           : juce::Colour (0xff6d6860);

        g.setColour (fillCol.withAlpha (0.9f));
        g.fillRoundedRectangle (fillRect, 2.0f);

        if (fight)
        {
            g.setColour (fillCol);
            g.drawRoundedRectangle (slot, 3.0f, 1.4f);
        }

        g.setColour (juce::Colour (0xff9c9689));
        g.setFont (juce::Font (10.0f, juce::Font::plain));
        g.drawText (encaje::kBands[(size_t) b].name,
                    juce::Rectangle<int> (col, area.getBottom() - 32, (int) bw, 14),
                    juce::Justification::centred);

        juce::String tag = mine        ? juce::String ("AL FRENTE")
                         : beingCut    ? (juce::String (adjDb, 1) + " dB")
                         : beingBoost  ? ("+" + juce::String (adjDb, 1) + " dB")
                                       : juce::String ("-");
        g.setColour (fillCol);
        g.setFont (juce::Font (9.5f, juce::Font::plain));
        g.drawText (tag, juce::Rectangle<int> (col, area.getBottom() - 18, (int) bw, 14),
                    juce::Justification::centred);
    }
}

void EncajeCarveAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (18);
    area.removeFromTop (58 + 10 + 46 + 8 + 170 + 12);

    auto knobRow = area.removeFromTop (104);
    const int quarter = knobRow.getWidth() / 4;

    auto a1 = knobRow.removeFromLeft (quarter).reduced (4);
    carveLabel.setBounds (a1.removeFromBottom (15));
    carveSlider.setBounds (a1);

    auto a2 = knobRow.removeFromLeft (quarter).reduced (4);
    maxCutLabel.setBounds (a2.removeFromBottom (15));
    maxCutSlider.setBounds (a2);

    auto a3 = knobRow.removeFromLeft (quarter).reduced (4);
    priorityLabel.setBounds (a3.removeFromBottom (15));
    prioritySlider.setBounds (a3);

    auto a4 = knobRow.reduced (4);
    targetSepLabel.setBounds (a4.removeFromBottom (15));
    targetSepSlider.setBounds (a4);

    area.removeFromTop (8);
    auto row1 = area.removeFromTop (26);
    roleLabel.setBounds (row1.removeFromLeft (44));
    roleBox.setBounds (row1.removeFromLeft (150));
    row1.removeFromLeft (14);
    groupLabel.setBounds (row1.removeFromLeft (48));
    groupBox.setBounds (row1.removeFromLeft (64));
    row1.removeFromLeft (14);
    enableButton.setBounds (row1);

    area.removeFromTop (6);
    auditionButton.setBounds (area.removeFromTop (24));
}
