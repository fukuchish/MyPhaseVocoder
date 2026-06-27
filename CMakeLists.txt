cmake_minimum_required(VERSION 3.22)
project(MyPhaseVocoder VERSION 0.0.1)

set(JUCE_DIR "C:/JUCE")
add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(MyPhaseVocoder
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE PhVo
    FORMATS VST3
    PRODUCT_NAME "MyPhaseVocoder"
    # 今回は純粋なオーディオエフェクトとして作るため MIDI入力は不要にします
    NEEDS_MIDI_INPUT FALSE
)

# JuceHeader.h を明示的に生成する
juce_generate_juce_header(MyPhaseVocoder)

target_sources(MyPhaseVocoder PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
)

target_compile_features(MyPhaseVocoder PRIVATE cxx_std_17)

# 文字コードの警告（C4819）を消すためのコンパイルオプション
if(MSVC)
    target_compile_options(MyPhaseVocoder PRIVATE /utf-8)
endif()

target_link_libraries(MyPhaseVocoder PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)

# VST2との互換性チェックを無効化
target_compile_definitions(MyPhaseVocoder PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
)