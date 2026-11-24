# No Mac: VST3 + AU. Noutros sistemas (só para testes de compilação): VST3.
if(APPLE)
    set(C00KUP_FORMATS AU VST3)
else()
    set(C00KUP_FORMATS VST3)
endif()

juce_add_plugin(C00KUP
    PRODUCT_NAME              "C00KUP"
    COMPANY_NAME              "808College"
    BUNDLE_ID                 com.808college.c00kup
    PLUGIN_MANUFACTURER_CODE  Cl8g
    PLUGIN_CODE               C00k
    FORMATS                   ${C00KUP_FORMATS}
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Fx Analyzer
    AU_MAIN_TYPE              kAudioUnitType_Effect
)

# Fontes (licença OFL) e símbolos SVG embutidos no próprio plugin: nada depende do sistema.
juce_add_binary_data(C00KUPAssets
    SOURCES
        Assets/SchibstedGrotesk-Medium.ttf
        Assets/SchibstedGrotesk-Bold.ttf
        Assets/SchibstedGrotesk-Black.ttf
        Assets/MartianMono-Regular.ttf
        Assets/simbolo-808-preto.svg
        Assets/c00kup-wordmark.svg
)

target_sources(C00KUP PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/C00kupLookAndFeel.cpp
    Source/WavRecorder.cpp
    Source/EngineLink.cpp
)

target_compile_definitions(C00KUP PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# O código-fonte está em UTF-8 (textos com acentos)
if(MSVC)
    target_compile_options(C00KUP PRIVATE /utf-8)
endif()

target_link_libraries(C00KUP
    PRIVATE
        C00KUPAssets
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
