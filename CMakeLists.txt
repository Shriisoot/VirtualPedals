cmake_minimum_required(VERSION 3.22)

project(VirtualPedals VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Path to a local JUCE checkout (framework is not vendored into this repo).
# Override with -DJUCE_PATH=/path/to/JUCE if it isn't checked out alongside this project.
set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to JUCE framework source")
add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)

juce_add_gui_app(VirtualPedals
    PRODUCT_NAME    "Virtual Pedals"
    COMPANY_NAME    "Virtual Pedals"
    BUNDLE_ID       com.virtualpedals.app
    VERSION         ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Virtual Pedals needs audio input for your guitar signal.")

juce_generate_juce_header(VirtualPedals)

target_sources(VirtualPedals PRIVATE
    Source/Main.cpp
    Source/App/MainComponent.cpp
    Source/Engine/AudioEngine.cpp
    Source/Engine/PedalChain.cpp
    Source/Engine/PedalFactory.cpp
    Source/Presets/PresetManager.cpp
    Source/ToneDna/ToneDnaAnalyzer.cpp
    Source/Hosting/PluginHost.cpp
    Source/Ui/PedalboardView.cpp
    Source/Ui/PedalComponent.cpp
    Source/Ui/SidePanels.cpp
    Source/Ui/NodeEditor.cpp)

target_include_directories(VirtualPedals PRIVATE Source)

target_compile_definitions(VirtualPedals PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:VirtualPedals,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:VirtualPedals,JUCE_VERSION>"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0        # VST2 needs the discontinued Steinberg SDK; headers not distributable
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_ASIO=0                  # enable after dropping Steinberg ASIO SDK into VP-tools (license-gated download)
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

if (MSVC)
    target_compile_options(VirtualPedals PRIVATE /W3 /bigobj)
endif()

target_link_libraries(VirtualPedals PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_recommended_config_flags)
