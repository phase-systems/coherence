cmake_minimum_required(VERSION 3.22)

# Apple Silicon and Intel Macs on macOS 11 or later.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Coherence VERSION 0.6.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(COHERENCE_BUILD_TESTS "Build the headless measurement and test runner" OFF)

set(COHERENCE_COMPANY "Phase Systems")

add_subdirectory(deps/JUCE)

if(APPLE)
    set(COHERENCE_FORMATS AU VST3)
else()
    set(COHERENCE_FORMATS VST3)
endif()

juce_add_plugin(Coherence
    COMPANY_NAME "${COHERENCE_COMPANY}"
    BUNDLE_ID com.phasesystems.coherence
    PLUGIN_MANUFACTURER_CODE Phsy
    PLUGIN_CODE Cohr
    FORMATS ${COHERENCE_FORMATS}
    PRODUCT_NAME "COHERENCE"
    DESCRIPTION "Console channel strip: EQ, dynamics, Mid/Side architecture"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx EQ Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

# DSP is plain C++ with no GUI dependency, so it is shared by the plug-in and the test runner.
set(COHERENCE_DSP_SOURCES
    Source/dsp/EqDesign.cpp
    Source/dsp/Engine.cpp)

set(COHERENCE_PLUGIN_SOURCES
    Source/Parameters.cpp
    Source/EqFocus.cpp
    Source/PluginProcessor.cpp
    Source/UserSettings.cpp
    Source/gui/Theme.cpp
    Source/gui/Controls.cpp
    Source/gui/Meters.cpp
    Source/PluginEditor.cpp)


target_sources(Coherence PRIVATE ${COHERENCE_DSP_SOURCES} ${COHERENCE_PLUGIN_SOURCES})
# The DSP compares smoothed values with their targets exactly on purpose (snapping), so -Wfloat-equal is noise.
if(NOT MSVC)
    set_source_files_properties(Source/dsp/Engine.cpp PROPERTIES COMPILE_OPTIONS "-Wno-float-equal")
endif()
target_include_directories(Coherence PRIVATE Source)

target_compile_definitions(Coherence PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(UNIX AND NOT APPLE)
    set(COHERENCE_LINUX_DEFS JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XRENDER=0 JUCE_USE_XCURSOR=0 JUCE_USE_XINPUT=0)
    target_compile_definitions(Coherence PUBLIC ${COHERENCE_LINUX_DEFS})
endif()

target_link_libraries(Coherence
    PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#-------------------------------------------------------------------------------
# Headless runner: measurements (EQ accuracy, routing, M/S, compressor, CPU) and the panel snapshot.
if(COHERENCE_BUILD_TESTS)
    juce_add_console_app(CoherenceTests PRODUCT_NAME "CoherenceTests")
    target_include_directories(CoherenceTests PRIVATE Source)
    target_compile_definitions(CoherenceTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="COHERENCE"
        JucePlugin_Manufacturer="${COHERENCE_COMPANY}"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JucePlugin_Build_Standalone=0
        ${COHERENCE_LINUX_DEFS})
    target_sources(CoherenceTests PRIVATE Tests/HeadlessTest.cpp ${COHERENCE_DSP_SOURCES} ${COHERENCE_PLUGIN_SOURCES})
    target_link_libraries(CoherenceTests
        PRIVATE
            juce::juce_audio_processors
                juce::juce_gui_basics
            juce::juce_recommended_config_flags)
endif()

#-------------------------------------------------------------------------------
# Format check: loads the installed AU and VST3 through the hosting APIs and compares them (build.sh runs it).
option(COHERENCE_BUILD_FORMAT_CHECK "Build the AU/VST3 consistency check" OFF)
if(COHERENCE_BUILD_FORMAT_CHECK)
    juce_add_console_app(CoherenceFormatCheck PRODUCT_NAME "CoherenceFormatCheck")
    target_sources(CoherenceFormatCheck PRIVATE Tests/FormatCheck.cpp)
    if(APPLE)
        set(COHERENCE_HOST_AU 1)
    else()
        set(COHERENCE_HOST_AU 0)
    endif()
    target_compile_definitions(CoherenceFormatCheck PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=${COHERENCE_HOST_AU}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        ${COHERENCE_LINUX_DEFS})
    target_link_libraries(CoherenceFormatCheck PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
    if(APPLE)
        target_link_libraries(CoherenceFormatCheck PRIVATE "-framework AudioUnit" "-framework CoreAudioKit")
    endif()
endif()
