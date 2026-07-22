cmake_minimum_required(VERSION 3.22)

project(Qube VERSION 0.1.0 LANGUAGES C CXX)

# -----------------------------------------------------------------------------
# Project-wide settings
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Universal binary on Apple silicon + Intel for distributable plugins. Can be
# overridden on the command line (e.g. -DCMAKE_OSX_ARCHITECTURES=arm64) for
# fast local iteration builds.
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    endif()
endif()

# -----------------------------------------------------------------------------
# Version / build identity baked into the binary.
# -----------------------------------------------------------------------------
find_package(Git QUIET)
set(QUBE_GIT_HASH "unknown")
set(QUBE_GIT_BRANCH "unknown")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE QUBE_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE QUBE_GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()

# -----------------------------------------------------------------------------
# JUCE (vendored as a git submodule under external/JUCE)
# -----------------------------------------------------------------------------
add_subdirectory(external/JUCE)

# clap-juce-extensions wraps the JUCE plugin as a CLAP alongside the
# AU/VST3/Standalone builds. EXCLUDE_FROM_ALL so only the CLAP target we
# attach below participates in the build.
add_subdirectory(external/clap-juce-extensions EXCLUDE_FROM_ALL)

# Local-install copy is on by default for developer iteration; CI builds set
# -DQUBE_INSTALL_LOCAL=OFF so they don't try to write into the user's
# plug-in directories.
option(QUBE_INSTALL_LOCAL "Copy built plugins into the user's plug-in dirs" ON)

juce_add_plugin(Qube
    COMPANY_NAME            "DatanoiseTV"
    BUNDLE_ID               "com.datanoisetv.qube"
    PLUGIN_MANUFACTURER_CODE Dnse
    PLUGIN_CODE             Qube
    FORMATS                 AU VST3 Standalone
    PRODUCT_NAME            "Qube"
    DESCRIPTION             "Quadraphonic spatial panner"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    NEEDS_WEB_BROWSER       TRUE   # pulls webkit2gtk + gtk pkg-config on Linux
    NEEDS_WEBVIEW2          TRUE   # Windows: statically link the WebView2 loader
    COPY_PLUGIN_AFTER_BUILD ${QUBE_INSTALL_LOCAL}
    VST3_CATEGORIES         Fx Spatial Surround
    AU_MAIN_TYPE            kAudioUnitType_Effect)

juce_generate_juce_header(Qube)

# -----------------------------------------------------------------------------
# CLAP wrapper — builds Qube.clap from the same shared plugin code. Creates a
# `Qube_CLAP` target (NOT part of Qube_All, so build it explicitly).
# -----------------------------------------------------------------------------
clap_juce_extensions_plugin(TARGET Qube
    CLAP_ID              "com.datanoisetv.qube"
    CLAP_FEATURES        audio-effect surround "spatial"
    CLAP_MANUAL_URL      "https://github.com/DatanoiseTV/qube"
    CLAP_SUPPORT_URL     "https://github.com/DatanoiseTV/qube/issues")

# -----------------------------------------------------------------------------
# UI assets — index.html / CSS / JSX / vendored React + Babel-standalone +
# the JUCE WebView frontend library. Served at runtime by
# WebBrowserComponent's resource provider, all bundled as BinaryData so the
# plugin never reaches the network. The vendored React UMDs and
# babel-standalone are MIT-licensed.
# -----------------------------------------------------------------------------
file(GLOB QUBE_UI_SRC    CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/src/*")
file(GLOB QUBE_UI_VENDOR CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/vendor/*")
juce_add_binary_data(QubeUI
    SOURCES     ${QUBE_UI_SRC} ${QUBE_UI_VENDOR}
    NAMESPACE   QubeUIData
    HEADER_NAME QubeUIData.h)
set_target_properties(QubeUI PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------
target_sources(Qube PRIVATE
    src/PluginProcessor.cpp
    src/dsp/SpatialEngine.cpp
    src/presets/PresetManager.cpp
    src/ui/WebEditor.cpp)

target_include_directories(Qube PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(Qube
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
        # Windows: NEEDS_WEBVIEW2 only LINKS the WebView2 loader; the module
        # compile flag must be set separately or juce_gui_extra falls back to
        # the legacy IE backend, which has no resource provider.
        $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>
    PRIVATE
        QUBE_VERSION="${PROJECT_VERSION}"
        QUBE_GIT_HASH="${QUBE_GIT_HASH}"
        QUBE_GIT_BRANCH="${QUBE_GIT_BRANCH}")

target_link_libraries(Qube PRIVATE
    QubeUI
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
# Unit tests (DSP cores + state round-trip). Built only when top-level.
# -----------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory(tests)
endif()
