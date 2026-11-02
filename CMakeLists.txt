cmake_minimum_required(VERSION 3.22)

project(Prisma VERSION 0.4.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PRISMA_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
option(PRISMA_BUILD_TESTS "Build the offline test program" OFF)

if(PRISMA_JUCE_DIR)
    add_subdirectory(${PRISMA_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(PRISMA_FORMATS VST3)
if(APPLE)
    list(APPEND PRISMA_FORMATS AU)
endif()

juce_add_plugin(Prisma
    COMPANY_NAME "Kamil Potebski"
    BUNDLE_ID "com.kamilpotebski.prisma"
    PLUGIN_MANUFACTURER_CODE Kpot
    PLUGIN_CODE Prsm
    FORMATS ${PRISMA_FORMATS}
    PRODUCT_NAME "Prisma"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

set(PRISMA_SOURCES
    Source/Sample.cpp
    Source/Hardware.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp)

target_sources(Prisma PRIVATE ${PRISMA_SOURCES})

set(PRISMA_DEFINES
    PRISMA_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(UNIX AND NOT APPLE)
    list(APPEND PRISMA_DEFINES JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
endif()

target_compile_definitions(Prisma PUBLIC ${PRISMA_DEFINES})

target_link_libraries(Prisma
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(PRISMA_BUILD_TESTS)
    juce_add_console_app(PrismaTest PRODUCT_NAME "PrismaTest")
    target_sources(PrismaTest PRIVATE Tests/OfflineTest.cpp ${PRISMA_SOURCES})
    target_compile_definitions(PrismaTest PRIVATE ${PRISMA_DEFINES}
        JucePlugin_Name="Prisma" JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
    target_link_libraries(PrismaTest PRIVATE
        juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_gui_basics
        juce::juce_recommended_config_flags)
endif()
