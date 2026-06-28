#include "PluginProcessor.h"
#include "PluginEditor.h"

MyPhaseVocoderAudioProcessor::MyPhaseVocoderAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Voice (Modulator)", juce::AudioChannelSet::stereo(), true)
          .withInput("Synth (Carrier)",   juce::AudioChannelSet::stereo(), true)
          .withOutput("Output",           juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameters()),
      fft(fftOrder), 
      window(fftSize, juce::dsp::WindowingFunction<float>::hann)
{
    spectrogramData.assign(numBins, 0.0f);
    drySpectrogramData.assign(numBins, 0.0f); // ★初期化を追加
}

MyPhaseVocoderAudioProcessor::~MyPhaseVocoderAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout MyPhaseVocoderAudioProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>("formant", "Formant Shift (st)", -12.0f, 12.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("sibilance", "Sibilance (%)", 0.0f, 100.0f, 50.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("sibilanceFreq", "Sibilance Freq (Hz)", 2000.0f, 10000.0f, 4000.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("mix", "Mix (%)", 0.0f, 100.0f, 50.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("gain", "Gain", -60.0f, 12.0f, 0.0f));
    return layout;
}

bool MyPhaseVocoderAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
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
        
        ch.tempModMag.assign(numBins, 0.0f);
        ch.shiftedModMag.assign(numBins, 0.0f);
        
        ch.writePointer = 0;
        ch.olaReadPointer = 0;
        ch.olaWritePointer = 0;
    }
    hopCounter = 0;
    nextFFTBlockReady = false;
}

void MyPhaseVocoderAudioProcessor::releaseResources() {}

void MyPhaseVocoderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    float gainLinear = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("gain")->load());
    float mixWet = apvts.getRawParameterValue("mix")->load() * 0.01f;
    float mixDry = 1.0f - mixWet;
    
    float formantSemitones = apvts.getRawParameterValue("formant")->load();
    float formantRatio = std::pow(2.0f, formantSemitones / 12.0f);
    float sibilanceVal = apvts.getRawParameterValue("sibilance")->load() * 0.01f;
    float sibilanceFreq = apvts.getRawParameterValue("sibilanceFreq")->load();

    double currentSampleRate = getSampleRate();
    if (currentSampleRate <= 0.0) currentSampleRate = 44100.0;
    int sibilanceBin = static_cast<int>(sibilanceFreq * fftSize / currentSampleRate);

    juce::AudioBuffer<float> modBus;
    juce::AudioBuffer<float> carBus;
    if (getBusCount(true) > 0) modBus = getBusBuffer(buffer, true, 0);
    if (getBusCount(true) > 1) carBus = getBusBuffer(buffer, true, 1);

    int numOutChannels = buffer.getNumChannels();

    juce::AudioBuffer<float> dryBuffer;
    if (modBus.getNumChannels() > 0) {
        dryBuffer.makeCopyOf(modBus);
    } else {
        dryBuffer.setSize(buffer.getNumChannels(), buffer.getNumSamples());
        dryBuffer.clear();
    }

    for (int s = 0; s < buffer.getNumSamples(); ++s)
    {
        for (int ch = 0; ch < std::min(numOutChannels, 2); ++ch)
        {
            auto& chData = channelData[ch];
            
            float modSample = (modBus.getNumChannels() > ch) ? modBus.getReadPointer(ch)[s] : 0.0f;
            float carSample = (carBus.getNumChannels() > ch) ? carBus.getReadPointer(ch)[s] : 0.0f;

            chData.modCircularBuffer[chData.writePointer] = modSample;
            chData.carCircularBuffer[chData.writePointer] = carSample;
            chData.writePointer = (chData.writePointer + 1) % fftSize;

            buffer.getWritePointer(ch)[s] = chData.olaBuffer[chData.olaReadPointer];
            chData.olaBuffer[chData.olaReadPointer] = 0.0f; 
            chData.olaReadPointer = (chData.olaReadPointer + 1) % fftSize;
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
                std::fill(chData.outFftBuffer.begin(), chData.outFftBuffer.end(), 0.0f);

                window.multiplyWithWindowingTable(chData.modFftBuffer.data(), fftSize);
                window.multiplyWithWindowingTable(chData.carFftBuffer.data(), fftSize);
                
                fft.performRealOnlyForwardTransform(chData.modFftBuffer.data());
                fft.performRealOnlyForwardTransform(chData.carFftBuffer.data());

                for (int k = 0; k < numBins; ++k) {
                    float mR = chData.modFftBuffer[2 * k];
                    float mI = chData.modFftBuffer[2 * k + 1];
                    chData.tempModMag[k] = std::sqrt(mR * mR + mI * mI);
                    
                    // ★修正: FFT直後の生のDRY（声）の振幅をUI用に保存
                    if (ch == 0) {
                        drySpectrogramData[k] = chData.tempModMag[k];
                    }
                }

                for (int k = 0; k < numBins; ++k) {
                    float srcK = k / formantRatio;
                    int idx = static_cast<int>(srcK);
                    float frac = srcK - idx;

                    if (idx >= 0 && idx < numBins - 1) {
                        chData.shiftedModMag[k] = chData.tempModMag[idx] * (1.0f - frac) + chData.tempModMag[idx + 1] * frac;
                    } else {
                        chData.shiftedModMag[k] = 0.0f;
                    }
                }

                for (int k = 0; k < numBins; ++k)
                {
                    float cR = chData.carFftBuffer[2 * k];
                    float cI = chData.carFftBuffer[2 * k + 1];
                    float carMag = std::sqrt(cR * cR + cI * cI);
                    float carPhase = std::atan2(cI, cR);

                    float outMag = (chData.shiftedModMag[k] * carMag) * 0.02f;
                    float outR = outMag * std::cos(carPhase);
                    float outI = outMag * std::sin(carPhase);

                    float sibWeight = 0.0f;
                    if (k > sibilanceBin) {
                        sibWeight = std::min(1.0f, (k - sibilanceBin) / 50.0f) * sibilanceVal;
                    }
                    
                    if (sibWeight > 0.0f) {
                        float originalMR = chData.modFftBuffer[2 * k];
                        float originalMI = chData.modFftBuffer[2 * k + 1];
                        outR = outR * (1.0f - sibWeight) + originalMR * sibWeight;
                        outI = outI * (1.0f - sibWeight) + originalMI * sibWeight;
                    }

                    chData.outFftBuffer[2 * k]     = outR;
                    chData.outFftBuffer[2 * k + 1] = outI;

                    // WET（合成後）の振幅をUI用に保存
                    if (ch == 0) {
                        spectrogramData[k] = std::sqrt(outR * outR + outI * outI);
                    }
                }

                if (ch == 0) {
                    nextFFTBlockReady = true;
                }

                fft.performRealOnlyInverseTransform(chData.outFftBuffer.data());
                window.multiplyWithWindowingTable(chData.outFftBuffer.data(), fftSize);

                for (int i = 0; i < fftSize; ++i) {
                    chData.olaBuffer[(chData.olaWritePointer + i) % fftSize] += chData.outFftBuffer[i] * 0.5f;
                }
                chData.olaWritePointer = (chData.olaWritePointer + hopSize) % fftSize;
            }
        }
    }

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

void MyPhaseVocoderAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MyPhaseVocoderAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* MyPhaseVocoderAudioProcessor::createEditor() { return new MyPhaseVocoderAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MyPhaseVocoderAudioProcessor(); }