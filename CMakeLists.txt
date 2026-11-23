cmake_minimum_required(VERSION 3.22)

project(Artifacts VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The build's identity, printed small on each plugin's plate, so it is plain which build a session has loaded
execute_process(COMMAND git rev-parse --short=7 HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE ARTIFACTS_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT ARTIFACTS_BUILD_ID)
    set(ARTIFACTS_BUILD_ID "local")
endif()
add_compile_definitions(ARTIFACTS_BUILD_ID="${ARTIFACTS_BUILD_ID}")

# Universal binary on macOS (Apple Silicon + Intel)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# Use a local JUCE checkout if JUCE_DIR is given, otherwise download it
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Embedded fonts, shared by every plugin
juce_add_binary_data(ArtifactsAssets SOURCES
    Resources/OldStandard-Regular.ttf
    Resources/OldStandard-Bold.ttf)
set_target_properties(ArtifactsAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(ARTIFACTS_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/Shared ${CMAKE_CURRENT_SOURCE_DIR}/UI)

# One plugin of the suite.  variant: 1 Kintsugi, 2 Console RA-1, 3 Duality, 4 Anemoia, 5 Equanimity
function(artifacts_plugin TARGET PRODUCT CODE VARIANT)
    string(TOLOWER ${TARGET} TARGET_LC)
    if(APPLE)
        set(FORMATS VST3 AU)
    else()
        set(FORMATS VST3)
    endif()
    juce_add_plugin(${TARGET}
        COMPANY_NAME "Reluctant Astronaut"
        BUNDLE_ID "com.reluctantastronaut.artifacts.${TARGET_LC}"
        PLUGIN_MANUFACTURER_CODE Rast
        PLUGIN_CODE ${CODE}
        FORMATS ${FORMATS}
        PRODUCT_NAME "${PRODUCT}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx ${ARGN})
    target_sources(${TARGET} PRIVATE UI/PluginEditor.cpp)
    target_include_directories(${TARGET} PRIVATE ${ARTIFACTS_INCLUDES})
    target_compile_definitions(${TARGET} PUBLIC
        ARTIFACTS_VARIANT=${VARIANT}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(${TARGET}
        PRIVATE
            ArtifactsAssets
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Offline test rig for this plugin: cmake -DARTIFACTS_TESTS=ON
    if(ARTIFACTS_TESTS)
        juce_add_console_app(${TARGET}Tests PRODUCT_NAME "${TARGET}Tests")
        target_sources(${TARGET}Tests PRIVATE Tests/SonicTests.cpp UI/PluginEditor.cpp)
        target_include_directories(${TARGET}Tests PRIVATE ${ARTIFACTS_INCLUDES} Tests)
        target_compile_definitions(${TARGET}Tests PRIVATE
            ARTIFACTS_VARIANT=${VARIANT}
            JucePlugin_Name="${PRODUCT}"
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(${TARGET}Tests PRIVATE
            ArtifactsAssets
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_recommended_config_flags)
        foreach(src ${ARGN_SOURCES})
            target_sources(${TARGET}Tests PRIVATE ${src})
        endforeach()
    endif()
endfunction()

option(ARTIFACTS_TESTS "Build the offline sonic tests" OFF)
include(Plugins/plugins.cmake)

# Deck RA-1 (the iPhone remote): renders its panel with the suite's drawing code. cmake -DARTIFACTS_TESTS=ON
if(ARTIFACTS_TESTS)
    juce_add_console_app(DeckRender PRODUCT_NAME "DeckRender")
    target_sources(DeckRender PRIVATE tools/Deck/render/DeckRender.cpp)
    target_include_directories(DeckRender PRIVATE ${ARTIFACTS_INCLUDES})
    target_compile_definitions(DeckRender PRIVATE JucePlugin_Name="Deck RA-1" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(DeckRender PRIVATE ArtifactsAssets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
