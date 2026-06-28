#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ==============================================================================
class MyPhaseVocoderAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    MyPhaseVocoderAudioProcessorEditor(MyPhaseVocoderAudioProcessor&);
    ~MyPhaseVocoderAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    MyPhaseVocoderAudioProcessor& audioProcessor;

    // ★ノブが5つになります
    juce::Slider formantSlider;
    juce::Slider sibilanceSlider;
    juce::Slider sibilanceFreqSlider; // 新規追加
    juce::Slider mixSlider;
    juce::Slider gainSlider;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<SliderAttachment> formantAttachment;
    std::unique_ptr<SliderAttachment> sibilanceAttachment;
    std::unique_ptr<SliderAttachment> sibilanceFreqAttachment; // 新規追加
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MyPhaseVocoderAudioProcessorEditor)
};