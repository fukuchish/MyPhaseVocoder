#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==============================================================================
MyPhaseVocoderAudioProcessor::MyPhaseVocoderAudioProcessor()
    // DAWに2つの入力を要求する（サイドチェイン用）
    : AudioProcessor(BusesProperties()
          .withInput("Voice (Modulator)", juce::AudioChannelSet::stereo(), true)
          .withInput("Synth (Carrier)",   juce::AudioChannelSet::stereo(), true)
          .withOutput("Output",           juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameters()),
      fft(fftOrder), 
      window(fftSize, juce::dsp::WindowingFunction<float>::hann)
{
}

MyPhaseVocoderAudioProcessor::~MyPhaseVocoderAudioProcessor() {}

// 今回は純粋なボコーダーにするため、パラメータはMixとGainのみ
juce::AudioProcessorValueTreeState::ParameterLayout MyPhaseVocoderAudioProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>("mix", "Mix (%)", 0.0f, 100.0f, 100.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("gain", "Gain", -60.0f, 12.0f, 0.0f));
    return layout;
}

bool MyPhaseVocoderAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono() && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void MyPhaseVocoderAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    
    for (auto& ch : channelData)
    {
        ch.modCircularBuffer.assign(fftSize, 0.0f);
        ch.carCircularBuffer.assign(fftSize, 0.0f);
        
        ch.modFftBuffer.assign(fftSize * 2, 0.0f);
        ch.carFftBuffer.assign(fftSize * 2, 0.0f);
        ch.outFftBuffer.assign(fftSize * 2, 0.0f);
        
        ch.olaBuffer.assign(fftSize, 0.0f);
        ch.writePointer = 0;
        ch.olaReadPointer = 0;
    }
    hopCounter = 0;
}

void MyPhaseVocoderAudioProcessor::releaseResources() {}

void MyPhaseVocoderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    float gainLinear = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("gain")->load());
    float mixWet = apvts.getRawParameterValue("mix")->load() * 0.01f;
    float mixDry = 1.0f - mixWet;

    // 2つのバス（VoiceとSynth）からオーディオを取得
    auto modBus = getBusBuffer(buffer, true, 0); 
    auto carBus = getBusBuffer(buffer, true, 1); 

    int numOutChannels = buffer.getNumChannels();
    int numBins = fftSize / 2 + 1;

    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(modBus);

    for (int s = 0; s < buffer.getNumSamples(); ++s)
    {
        for (int ch = 0; ch < std::min(numOutChannels, 2); ++ch)
        {
            auto& chData = channelData[ch];
            
            // モジュレーターとキャリアそれぞれのサンプルを取得
            float modSample = (modBus.getNumChannels() > ch) ? modBus.getReadPointer(ch)[s] : 0.0f;
            float carSample = (carBus.getNumChannels() > ch) ? carBus.getReadPointer(ch)[s] : 0.0f;

            chData.modCircularBuffer[chData.writePointer] = modSample;
            chData.carCircularBuffer[chData.writePointer] = carSample;
            
            chData.writePointer = (chData.writePointer + 1) % fftSize;

            float outputSample = chData.olaBuffer[chData.olaReadPointer];
            chData.olaBuffer[chData.olaReadPointer] = 0.0f; 
            chData.olaReadPointer = (chData.olaReadPointer + 1) % fftSize;

            buffer.getWritePointer(ch)[s] = outputSample;
        }

        hopCounter++;
        if (hopCounter >= hopSize)
        {
            hopCounter = 0;

            for (int ch = 0; ch < std::min(numOutChannels, 2); ++ch)
            {
                auto& chData = channelData[ch];

                for (int i = 0; i < fftSize; ++i) {
                    chData.modFftBuffer[i] = chData.modCircularBuffer[(chData.writePointer + i) % fftSize];
                    chData.carFftBuffer[i] = chData.carCircularBuffer[(chData.writePointer + i) % fftSize];
                }
                std::fill(chData.modFftBuffer.begin() + fftSize, chData.modFftBuffer.end(), 0.0f);
                std::fill(chData.carFftBuffer.begin() + fftSize, chData.carFftBuffer.end(), 0.0f);

                window.multiplyWithWindowingTable(chData.modFftBuffer.data(), fftSize);
                window.multiplyWithWindowingTable(chData.carFftBuffer.data(), fftSize);
                
                fft.performRealOnlyForwardTransform(chData.modFftBuffer.data());
                fft.performRealOnlyForwardTransform(chData.carFftBuffer.data());

                // 【クロスシンセシス (Cross-Synthesis)】
                for (int k = 0; k < numBins; ++k)
                {
                    // モジュレーター（声）の振幅
                    float mR = chData.modFftBuffer[2 * k];
                    float mI = chData.modFftBuffer[2 * k + 1];
                    float modMag = std::sqrt(mR * mR + mI * mI);

                    // キャリア（シンセ）の振幅と位相
                    float cR = chData.carFftBuffer[2 * k];
                    float cI = chData.carFftBuffer[2 * k + 1];
                    float carMag = std::sqrt(cR * cR + cI * cI);
                    float carPhase = std::atan2(cI, cR);

                    // 振幅の掛け合わせ（スケーリング係数 0.02f は適宜調整）
                    float outMag = (modMag * carMag) * 0.02f;

                    // キャリアの位相を使って再構築
                    chData.outFftBuffer[2 * k]     = outMag * std::cos(carPhase);
                    chData.outFftBuffer[2 * k + 1] = outMag * std::sin(carPhase);
                }

                fft.performRealOnlyInverseTransform(chData.outFftBuffer.data());
                window.multiplyWithWindowingTable(chData.outFftBuffer.data(), fftSize);

                for (int i = 0; i < fftSize; ++i) {
                    chData.olaBuffer[(chData.olaReadPointer + i) % fftSize] += chData.outFftBuffer[i] * 0.5f;
                }
            }
        }
    }

    // MixとGainの処理
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        if (ch >= 2) { buffer.clear(ch, 0, buffer.getNumSamples()); continue; }
        auto* outData = buffer.getWritePointer(ch);
        auto* dryData = dryBuffer.getReadPointer(ch);
        for (int s = 0; s < buffer.getNumSamples(); ++s) {
            float mixed = (dryData[s] * mixDry) + (outData[s] * mixWet);
            outData[s] = std::tanh(mixed) * gainLinear;
        }
    }
}

juce::AudioProcessorEditor* MyPhaseVocoderAudioProcessor::createEditor() { return new MyPhaseVocoderAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MyPhaseVocoderAudioProcessor(); }