# -----------------------------------------------------------------------------
# Lightweight DSP unit tests. The spatial cores (panner, motion, binaural,
# UHJ, room, engine) are framework-free, so these build and run in seconds
# without JUCE.
# -----------------------------------------------------------------------------
add_executable(qube_dsp_tests
    test_dsp.cpp
    ${CMAKE_SOURCE_DIR}/src/dsp/SpatialEngine.cpp)

target_include_directories(qube_dsp_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(qube_dsp_tests PRIVATE cxx_std_20)

add_test(NAME dsp COMMAND qube_dsp_tests)

# -----------------------------------------------------------------------------
# State / preset tests. Linking `Qube` cascades JUCE's INTERFACE_SOURCES into
# the test target; free on macOS/Windows, but on Linux it would force a
# webkit2gtk build — so Apple/Windows hosts only (the logic is platform-
# independent; macOS CI catches regressions).
# -----------------------------------------------------------------------------
if(APPLE OR WIN32)
    add_executable(qube_state_tests test_state.cpp)
    target_include_directories(qube_state_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_features(qube_state_tests PRIVATE cxx_std_20)
    target_link_libraries(qube_state_tests PRIVATE
        Qube
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    add_test(NAME state COMMAND qube_state_tests)

    # Windows: the cascaded juce_gui_extra.cpp + the PUBLIC
    # JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING define need the WebView2 SDK
    # in the test target too.
    if(WIN32)
        target_link_libraries(qube_state_tests PRIVATE juce::juce_webview2)
    endif()
endif()
