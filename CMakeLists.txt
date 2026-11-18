cmake_minimum_required(VERSION 3.22)
project(P6Panel VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/CMakeLists.txt")
    add_subdirectory(../JUCE JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# An audio effect that passes audio through and talks to the Prophet-6 over its own MIDI ports.
juce_add_plugin(P6Panel
    COMPANY_NAME "Claude"
    PLUGIN_MANUFACTURER_CODE Clau
    PLUGIN_CODE P6pn
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Prophet 6 Panel"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "The standalone panel passes the audio-interface input (your Prophet-6) through to the output so you can hear the synth."
    VST3_CATEGORIES Fx Tools)

target_sources(P6Panel PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EuclidPage.cpp
    Source/MidiLink.cpp
    Source/MoogLook.cpp)

target_compile_definitions(P6Panel PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_USE_XRANDR=0
    JUCE_USE_XINERAMA=0
    JUCE_USE_XCURSOR=0
    JUCE_USE_XSHM=0
    JUCE_USE_FONTCONFIG=0)
if(NOT APPLE)
    target_compile_definitions(P6Panel PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
endif()

target_link_libraries(P6Panel PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_gui_basics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(P6V2_BUILD_TEST)
    juce_add_console_app(P6V2Test PRODUCT_NAME "P6V2Test")
    target_sources(P6V2Test PRIVATE Test/main.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/EuclidPage.cpp Source/MidiLink.cpp Source/MoogLook.cpp)
    target_include_directories(P6V2Test PRIVATE Source)
    target_compile_definitions(P6V2Test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0 JUCE_USE_FONTCONFIG=0 JucePlugin_Name="P6V2Test" JucePlugin_Manufacturer="Claude" JucePlugin_IsSynth=0 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=1 JucePlugin_IsMidiEffect=0 JucePlugin_EditorRequiresKeyboardFocus=1 JucePlugin_Build_Standalone=1 JUCE_ALSA=0 JUCE_JACK=0 JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries(P6V2Test PRIVATE juce::juce_audio_processors juce::juce_audio_devices juce::juce_audio_utils juce::juce_gui_basics PUBLIC juce::juce_recommended_config_flags)
    # the EUCLID engine on its own (no GUI, no MIDI devices)
    juce_add_console_app(EuclidTest PRODUCT_NAME "EuclidTest")
    target_sources(EuclidTest PRIVATE Test/euclid.cpp)
    target_include_directories(EuclidTest PRIVATE Source)
    target_compile_definitions(EuclidTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(EuclidTest PRIVATE juce::juce_audio_basics juce::juce_data_structures juce::juce_core PUBLIC juce::juce_recommended_config_flags)
endif()
