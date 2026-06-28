#include "PluginProcessor.h"
#include "PluginEditor.h"

MyPhaseVocoderAudioProcessorEditor::MyPhaseVocoderAudioProcessorEditor(MyPhaseVocoderAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(750, 450);

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

    setupSlider(formantSlider,       formantAttachment,       "formant",       juce::Colours::magenta,     " st");
    setupSlider(sibilanceSlider,     sibilanceAttachment,     "sibilance",     juce::Colours::yellow,      " %");
    setupSlider(sibilanceFreqSlider, sibilanceFreqAttachment, "sibilanceFreq", juce::Colours::orange,      " Hz");
    setupSlider(mixSlider,           mixAttachment,           "mix",           juce::Colours::lightgreen,  " %");
    setupSlider(gainSlider,          gainAttachment,          "gain",          juce::Colours::dodgerblue,  " dB");

    startTimerHz(50);
}

MyPhaseVocoderAudioProcessorEditor::~MyPhaseVocoderAudioProcessorEditor()
{
    stopTimer();
}

void MyPhaseVocoderAudioProcessorEditor::timerCallback()
{
    if (audioProcessor.nextFFTBlockReady.exchange(false))
    {
        repaint(analyzerBounds);
    }
}

void MyPhaseVocoderAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(30, 30, 40));

    g.setColour(juce::Colours::white);
    g.setFont(24.0f);
    g.drawFittedText("MyPhaseVocoder", getLocalBounds().removeFromTop(40), juce::Justification::centred, 1);

    g.setFont(14.0f);
    int knobSize = 100;
    int startY = 60;
    int spacing = getWidth() / 5;

    g.drawText("Formant",    0 * spacing, startY + knobSize, spacing, 20, juce::Justification::centred);
    g.drawText("Sibilance Amount", 1 * spacing, startY + knobSize, spacing, 20, juce::Justification::centred);
    g.drawText("Sibilance Freq",   2 * spacing, startY + knobSize, spacing, 20, juce::Justification::centred);
    g.drawText("Mix",        3 * spacing, startY + knobSize, spacing, 20, juce::Justification::centred);
    g.drawText("Gain",       4 * spacing, startY + knobSize, spacing, 20, juce::Justification::centred);

    // ----------------------------------------------------------------------
    // ★EQアナライザー描画
    // ----------------------------------------------------------------------
    g.setColour(juce::Colours::black);
    g.fillRect(analyzerBounds);

    float minFreq = 20.0f;
    float maxFreq = 30000.0f;
    auto getNormX = [minFreq, maxFreq](float hz) {
        return (std::log10(std::max(minFreq, hz)) - std::log10(minFreq)) / (std::log10(maxFreq) - std::log10(minFreq));
    };

    // ★修正: Y軸の表示範囲をプロ仕様の「-80dB 〜 +20dB」に拡張
    float minDb = -100.0f;
    float maxDb = 20.0f;
    auto getNormY = [minDb, maxDb](float db) {
        return juce::jmap(db, minDb, maxDb, 1.0f, 0.0f);
    };

    g.setFont(10.0f);
    g.setColour(juce::Colour::fromRGB(45, 45, 55)); // 補助線の色

    // ★修正: dB補助線 (-60, -40, -20, 0, 20)
    std::array<float, 6> dbLines = { -80.0f, -60.0f, -40.0f, -20.0f, 0.0f, 20.0f };
    for (float dbTarget : dbLines)
    {
        float normY = getNormY(dbTarget);
        if (normY >= 0.0f && normY <= 1.0f) {
            float yPos = analyzerBounds.getY() + analyzerBounds.getHeight() * normY;
            g.drawHorizontalLine(static_cast<int>(yPos), static_cast<float>(analyzerBounds.getX()), static_cast<float>(analyzerBounds.getRight()));
            g.drawText(juce::String(static_cast<int>(dbTarget)), analyzerBounds.getX() + 2, static_cast<int>(yPos) - 12, 30, 12, juce::Justification::left);
        }
    }

    // 周波数の縦補助線
    struct FreqLine { float hz; juce::String label; };
    std::array<FreqLine, 11> hzLines = {
        FreqLine{20.f, "20"}, FreqLine{50.f, "50"}, FreqLine{100.f, "100"}, FreqLine{250.f, "250"}, FreqLine{500.f, "500"},
        FreqLine{1000.f, "1k"}, FreqLine{2000.f, "2k"}, FreqLine{4000.f, "4k"}, FreqLine{10000.f, "10k"}, FreqLine{20000.f, "20k"}, FreqLine{30000.f, "30k"}
    };
    
    for (const auto& fLine : hzLines)
    {
        float normX = getNormX(fLine.hz);
        if (normX >= 0.0f && normX <= 1.0f) {
            float xPos = analyzerBounds.getX() + analyzerBounds.getWidth() * normX;
            g.drawVerticalLine(static_cast<int>(xPos), static_cast<float>(analyzerBounds.getY()), static_cast<float>(analyzerBounds.getBottom()));
            g.drawText(fLine.label, static_cast<int>(xPos) + 2, analyzerBounds.getBottom() - 12, 30, 12, juce::Justification::left);
        }
    }

    // ★修正: DRY（声）とWET（合成後）の2つのPathを用意
    juce::Path dryPath, wetPath;
    bool firstPoint = true;
    float lastX = analyzerBounds.getX();
    
    double sampleRate = audioProcessor.getSampleRate();
    if (sampleRate <= 0.0) sampleRate = 44100.0;

    for (int k = 0; k < MyPhaseVocoderAudioProcessor::numBins; ++k)
    {
        float hz = static_cast<float>(k) * static_cast<float>(sampleRate) / MyPhaseVocoderAudioProcessor::fftSize;
        if (hz < minFreq) hz = minFreq; 
        if (hz > maxFreq) continue;     

        float normX = getNormX(hz);
        float x = analyzerBounds.getX() + analyzerBounds.getWidth() * normX;
        lastX = x;

        // DRYの計算 (正規化とdB変換)
        float dryMag = audioProcessor.drySpectrogramData[k] * (2.0f / MyPhaseVocoderAudioProcessor::fftSize);
        float dryDb = juce::Decibels::gainToDecibels(dryMag, -100.0f);
        float dryY = analyzerBounds.getY() + analyzerBounds.getHeight() * juce::jlimit(0.0f, 1.0f, getNormY(dryDb));

        // WETの計算
        float wetMag = audioProcessor.spectrogramData[k] * (2.0f / MyPhaseVocoderAudioProcessor::fftSize);
        float wetDb = juce::Decibels::gainToDecibels(wetMag, -100.0f);
        float wetY = analyzerBounds.getY() + analyzerBounds.getHeight() * juce::jlimit(0.0f, 1.0f, getNormY(wetDb));

        if (firstPoint) {
            dryPath.startNewSubPath(x, dryY);
            wetPath.startNewSubPath(x, wetY);
            firstPoint = false;
        } else {
            dryPath.lineTo(x, dryY);
            wetPath.lineTo(x, wetY);
        }
    }

    // 1. DRY（元の声）の描画：背景にグレーの線で描く
    g.setColour(juce::Colours::grey.withAlpha(0.8f));
    g.strokePath(dryPath, juce::PathStrokeType(1.0f));

    // 2. WET（合成後）の描画：シアンで半透明フィル＋線を描画
    if (!wetPath.isEmpty())
    {
        juce::Path filledWetPath = wetPath;
        filledWetPath.lineTo(lastX, analyzerBounds.getBottom());
        filledWetPath.lineTo(analyzerBounds.getX(), analyzerBounds.getBottom());
        filledWetPath.closeSubPath();
        
        g.setColour(juce::Colours::cyan.withAlpha(0.3f));
        g.fillPath(filledWetPath);
    }

    g.setColour(juce::Colours::cyan);
    g.strokePath(wetPath, juce::PathStrokeType(1.5f));

    // アナライザーの外枠
    g.setColour(juce::Colour::fromRGB(70, 70, 80));
    g.drawRect(analyzerBounds.expanded(1));
}

void MyPhaseVocoderAudioProcessorEditor::resized()
{
    int knobSize = 100;
    int startY = 60;
    int spacing = getWidth() / 5;
    int offsetX = (spacing - knobSize) / 2;

    formantSlider.setBounds      (0 * spacing + offsetX, startY, knobSize, knobSize);
    sibilanceSlider.setBounds    (1 * spacing + offsetX, startY, knobSize, knobSize);
    sibilanceFreqSlider.setBounds(2 * spacing + offsetX, startY, knobSize, knobSize);
    mixSlider.setBounds          (3 * spacing + offsetX, startY, knobSize, knobSize);
    gainSlider.setBounds         (4 * spacing + offsetX, startY, knobSize, knobSize);

    analyzerBounds = juce::Rectangle<int>(40, 230, getWidth() - 80, 180);
}