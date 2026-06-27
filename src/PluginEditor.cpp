#include "PluginProcessor.h"
#include "PluginEditor.h"

MyPhaseVocoderAudioProcessorEditor::MyPhaseVocoderAudioProcessorEditor(MyPhaseVocoderAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(400, 300);

    auto setupSlider = [this](juce::Slider& slider, std::unique_ptr<SliderAttachment>& attachment, const juce::String& paramID, juce::Colour fillColour, const juce::String& suffix)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
        
        slider.setColour(juce::Slider::rotarySliderFillColourId, fillColour);
        slider.setColour(juce::Slider::thumbColourId, juce::Colours::lightgrey);
        slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(50, 50, 60));
        
        slider.setTextValueSuffix(suffix);
        
        addAndMakeVisible(slider);
        attachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, paramID, slider);
    };

    setupSlider(mixSlider, mixAttachment,     "mix",   juce::Colours::lightgreen, " %");
    setupSlider(gainSlider, gainAttachment,   "gain",  juce::Colours::dodgerblue, " dB");
}

MyPhaseVocoderAudioProcessorEditor::~MyPhaseVocoderAudioProcessorEditor() {}

void MyPhaseVocoderAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(30, 30, 40));

    g.setColour(juce::Colours::white);
    g.setFont(24.0f);
    g.drawFittedText("FFT VOCODER (N=512)", getLocalBounds().removeFromTop(60), juce::Justification::centred, 1);

    g.setFont(14.0f);
    int knobSize = 100;
    int startY = 100;
    int spacing = getWidth() / 2;

    g.drawText("Mix",          0 * spacing, startY + knobSize, spacing, 20, juce::Justification::centred);
    g.drawText("Gain",         1 * spacing, startY + knobSize, spacing, 20, juce::Justification::centred);
}

void MyPhaseVocoderAudioProcessorEditor::resized()
{
    int knobSize = 100;
    int startY = 100;
    int spacing = getWidth() / 2;
    int offsetX = (spacing - knobSize) / 2;

    mixSlider.setBounds   (0 * spacing + offsetX, startY, knobSize, knobSize);
    gainSlider.setBounds  (1 * spacing + offsetX, startY, knobSize, knobSize);
}