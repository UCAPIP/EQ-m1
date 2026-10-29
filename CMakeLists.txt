cmake_minimum_required(VERSION 3.22)

project(EQ_M1
    VERSION 1.0.0
    DESCRIPTION "EQ-m1 - parametric equalizer plugin (FabFilter Pro-Q style)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(EQM1_BUILD_TESTS "Build the framework-free DSP test binary" ON)
option(EQM1_BUILD_PLUGIN "Build the VST3/Standalone plugin (requires JUCE)" ON)

# ---------------------------------------------------------------------------
# DSP core: no JUCE, no framework dependencies -> unit-testable anywhere.
# ---------------------------------------------------------------------------
add_library(EQM1DSP STATIC
    Source/DSP/Biquad.cpp
    Source/DSP/Curve.cpp
    Source/DSP/EqEngine.cpp)

target_include_directories(EQM1DSP PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_features(EQM1DSP PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(EQM1DSP PRIVATE /W4)
else()
    target_compile_options(EQM1DSP PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EQM1_BUILD_TESTS)
    enable_testing()
    add_executable(EQM1Tests Tests/DspTests.cpp)
    target_link_libraries(EQM1Tests PRIVATE EQM1DSP)
    if(MSVC)
        target_compile_options(EQM1Tests PRIVATE /W4)
    else()
        target_compile_options(EQM1Tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME EQM1DspTests COMMAND EQM1Tests)
endif()

if(NOT EQM1_BUILD_PLUGIN)
    return()
endif()

# ---------------------------------------------------------------------------
# JUCE + plugin
# ---------------------------------------------------------------------------
include(FetchContent)

set(EQM1_JUCE_TAG "8.0.16-security" CACHE STRING "JUCE git tag to build against")
set(EQM1_JUCE_DIR "" CACHE PATH "Optional path to an existing local JUCE checkout")

if(NOT "${EQM1_JUCE_DIR}" STREQUAL "")
    add_subdirectory("${EQM1_JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${EQM1_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(EQ_M1
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "EQ-m1"
    PLUGIN_MANUFACTURER_CODE Eqm1
    PLUGIN_CODE EqM1
    FORMATS VST3 Standalone
    PRODUCT_NAME "EQ-m1"
    DESCRIPTION "Parametric equalizer with per-band solo monitoring"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANT_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Fx" "Equalizer")

juce_generate_juce_header(EQ_M1)

target_sources(EQ_M1 PRIVATE
    Source/Processor/Params.cpp
    Source/Processor/EqM1AudioProcessor.cpp
    Source/Processor/SpectrumAnalyser.cpp
    Source/Editor/EqM1AudioProcessorEditor.cpp
    Source/Editor/EqView.cpp
    Source/Editor/ControlBar.cpp
    Source/PluginMain.cpp)

target_include_directories(EQ_M1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(EQ_M1 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    DONT_SET_USING_JUCE_NAMESPACE=1)

target_link_libraries(EQ_M1 PRIVATE
    EQM1DSP
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

# The warning set is spelled out instead of linking juce_recommended_warning_flags: that
# target appends -Wfloat-equal and -Wswitch-enum after the target's own options, and both
# fight deliberate choices here - exact float comparison is how parameter changes are
# detected between blocks, and the switches over FilterType/ChannelMode/Slope handle the
# `count` sentinel through their default branch.
if(MSVC)
    set(EQM1_WARNINGS /W4)
else()
    set(EQM1_WARNINGS -Wall -Wextra -Wshadow -Wuninitialized -Wstrict-aliasing)
endif()

target_compile_options(EQ_M1 PRIVATE ${EQM1_WARNINGS})

# The headless integration binary only makes sense on the host platform, so it is skipped
# for cross builds (e.g. a Windows target compiled from Linux with mingw-w64).
if(EQM1_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    juce_add_console_app(EQM1Integration
        PRODUCT_NAME "EQ-m1 Integration Tests")

    juce_generate_juce_header(EQM1Integration)

    target_sources(EQM1Integration PRIVATE
        Source/Processor/Params.cpp
        Source/Processor/EqM1AudioProcessor.cpp
        Source/Processor/SpectrumAnalyser.cpp
        Source/Editor/EqM1AudioProcessorEditor.cpp
        Source/Editor/EqView.cpp
        Source/Editor/ControlBar.cpp
        Tests/IntegrationTests.cpp)

    target_include_directories(EQM1Integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

    target_compile_definitions(EQM1Integration PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        DONT_SET_USING_JUCE_NAMESPACE=1)

    target_link_libraries(EQM1Integration PRIVATE
        EQM1DSP
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)

    target_compile_options(EQM1Integration PRIVATE ${EQM1_WARNINGS})
endif()

