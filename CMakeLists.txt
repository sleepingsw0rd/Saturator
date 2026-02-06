cmake_minimum_required(VERSION 3.22)
project(Saturator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch JUCE from GitHub
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# Plugin target
juce_add_plugin(Saturator
    COMPANY_NAME            "YourCompany"
    PLUGIN_MANUFACTURER_CODE Yoco
    PLUGIN_CODE              Satr
    FORMATS                  VST3 Standalone
    PRODUCT_NAME             "Saturator"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
)

target_sources(Saturator PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SaturatorDSP.cpp
)

target_compile_definitions(Saturator PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Saturator
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
