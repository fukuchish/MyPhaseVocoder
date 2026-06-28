#pragma once
#include <JuceHeader.h>
#include <vector>
#include <array>

// ==============================================================================
// FFT クロスシンセシス・ボコーダー (フォルマント＆シビランス追加版)
// ==============================================================================
class MyPhaseVocoderAudioProcessor : public juce::AudioProcessor
{
public:
    MyPhaseVocoderAudioProcessor();
    ~MyPhaseVocoderAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FFT Sidechain Vocoder"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();

    static constexpr int fftOrder = 11; 
    static constexpr int fftSize = 1 << fftOrder;     // 2048 サンプル
    static constexpr int overlap = 4;                 
    static constexpr int hopSize = fftSize / overlap; // 512 サンプル

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    struct ChannelData {
        std::vector<float> modCircularBuffer;
        std::vector<float> carCircularBuffer;
        
        std::vector<float> modFftBuffer;
        std::vector<float> carFftBuffer;
        std::vector<float> outFftBuffer;
        
        std::vector<float> olaBuffer;

        // ★新規追加: フォルマント計算用の作業バッファ
        std::vector<float> tempModMag;
        std::vector<float> shiftedModMag;

        int writePointer = 0;
        int olaReadPointer = 0;
        int olaWritePointer = 0;
    };

    std::array<ChannelData, 2> channelData;
    int hopCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MyPhaseVocoderAudioProcessor)
};