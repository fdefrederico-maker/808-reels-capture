# No Mac: VST3 + AU. Noutros sistemas (só para testes de compilação): VST3.
if(APPLE)
    set(REELS_FORMATS AU VST3)
else()
    set(REELS_FORMATS VST3)
endif()

juce_add_plugin(ReelsCapture
    PRODUCT_NAME              "808 Reels Capture"
    COMPANY_NAME              "808 Reels"
    BUNDLE_ID                 com.reels808.capture
    PLUGIN_MANUFACTURER_CODE  Rels
    PLUGIN_CODE               R8cp
    FORMATS                   ${REELS_FORMATS}
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Fx Analyzer
    AU_MAIN_TYPE              kAudioUnitType_Effect
)

target_sources(ReelsCapture PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/WavRecorder.cpp
    Source/EngineLink.cpp
)

target_compile_definitions(ReelsCapture PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(ReelsCapture
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
