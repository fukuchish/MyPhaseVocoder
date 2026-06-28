#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class MyPhaseVocoderAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           public juce::Timer
{
public:
    MyPhaseVocoderAudioProcessorEditor(MyPhaseVocoderAudioProcessor&);
    ~MyPhaseVocoderAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    
    void timerCallback() override;

private:
    MyPhaseVocoderAudioProcessor& audioProcessor;

    juce::Slider formantSlider;
    juce::Slider sibilanceSlider;
    juce::Slider sibilanceFreqSlider;
    juce::Slider mixSlider;
    juce::Slider gainSlider;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<SliderAttachment> formantAttachment;
    std::unique_ptr<SliderAttachment> sibilanceAttachment;
    std::unique_ptr<SliderAttachment> sibilanceFreqAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment;

    // ★修正: 重いImageキャッシュを廃止し、純粋なベクターバウンズのみを定義
    juce::Rectangle<int> analyzerBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MyPhaseVocoderAudioProcessorEditor)
};