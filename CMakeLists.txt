cmake_minimum_required(VERSION 3.24)
project(MakeSynth VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(MAKE_SYNTH_BUILD_PLUGIN "Build the VST3 and offline renderer" ON)
include(CTest)
if(BUILD_TESTING)
    add_executable(MakeSynthDspTests Tests/DspTests.cpp)
    target_include_directories(MakeSynthDspTests PRIVATE Source)
    add_test(NAME dsp COMMAND MakeSynthDspTests)
endif()
if(NOT MAKE_SYNTH_BUILD_PLUGIN)
    return()
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    URL https://codeload.github.com/juce-framework/JUCE/tar.gz/72782788ce18c2d4d760b28e0921d6ffc6431102
    URL_HASH SHA256=28ec8ae0626a4d1c6ca8c32ad13f95868b147779d4f3df4a7868f333a38e0551
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MakeSynth
    COMPANY_NAME "Make Synth"
    BUNDLE_ID "org.makesynth.instrument"
    PLUGIN_MANUFACTURER_CODE Mksy
    PLUGIN_CODE Msdn
    PRODUCT_NAME "Make Synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_CATEGORIES Instrument Synth
    FORMATS VST3)
target_sources(MakeSynth PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(MakeSynth PUBLIC Source)
target_compile_definitions(MakeSynth PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_ALSA=0 JUCE_JACK=0
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
target_link_libraries(MakeSynth PRIVATE juce::juce_audio_processors juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)

add_executable(MakeSynthRender Tools/Render.cpp)
set_target_properties(MakeSynthRender PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/MakeSynthRender_artefacts/$<CONFIG>")
target_link_libraries(MakeSynthRender PRIVATE MakeSynth)
target_include_directories(MakeSynthRender PRIVATE $<TARGET_PROPERTY:MakeSynth,INCLUDE_DIRECTORIES>)
target_compile_definitions(MakeSynthRender PRIVATE $<TARGET_PROPERTY:MakeSynth,COMPILE_DEFINITIONS>)
if(BUILD_TESTING)
    add_test(NAME processor COMMAND MakeSynthRender --test)
endif()
