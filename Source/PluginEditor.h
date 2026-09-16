#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class EncajeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EncajeLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff1c1b1f));
        setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xffe3b23c));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3c3841));
        setColour (juce::Slider::thumbColourId,               juce::Colour (0xffe3b23c));
        setColour (juce::Slider::textBoxTextColourId,         juce::Colour (0xffece7dc));
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colour (0xff3c3841));
        setColour (juce::ComboBox::backgroundColourId,        juce::Colour (0xff242229));
        setColour (juce::ComboBox::outlineColourId,           juce::Colour (0xff3c3841));
        setColour (juce::Label::textColourId,                 juce::Colour (0xffece7dc));
        setColour (juce::ComboBox::textColourId,              juce::Colour (0xffece7dc));
        setColour (juce::PopupMenu::backgroundColourId,       juce::Colour (0xff242229));
        setColour (juce::PopupMenu::textColourId,             juce::Colour (0xffece7dc));
        setColour (juce::ToggleButton::textColourId,          juce::Colour (0xffece7dc));
        setColour (juce::ToggleButton::tickColourId,          juce::Colour (0xffe3b23c));
    }
};

class EncajeCarveAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit EncajeCarveAudioProcessorEditor (EncajeCarveAudioProcessor&);
    ~EncajeCarveAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawBandMeters (juce::Graphics&, juce::Rectangle<int> area);

    EncajeCarveAudioProcessor& processor;
    EncajeLookAndFeel laf;

    juce::Slider   carveSlider, maxCutSlider, prioritySlider;
    juce::Label    carveLabel,  maxCutLabel,  priorityLabel;
    juce::ComboBox roleBox, groupBox;
    juce::Label    roleLabel, groupLabel;
    juce::ToggleButton enableButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   carveAtt, maxCutAtt, priorityAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> roleAtt, groupAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   enableAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EncajeCarveAudioProcessorEditor)
};
