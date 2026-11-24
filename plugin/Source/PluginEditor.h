#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "C00kupLookAndFeel.h"

// Janela do C00KUP (760 × 600), desenhada segundo design-c00kup/SPEC.md:
// lombada vermelha, loudness, medidores L/R, controlos de vídeo, knobs,
// pré-visualização 9:16 ao vivo e rodapé com Gravar / Parar / Abrir pasta.
class ReelsCaptureEditor : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private juce::FocusChangeListener
{
public:
    explicit ReelsCaptureEditor (ReelsCaptureProcessor&);
    ~ReelsCaptureEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    void timerCallback() override;
    void globalFocusChanged (juce::Component*) override;

    void updateButtons();
    void layoutFooter();
    void syncVideoControls();
    void chooseBackgroundImage();
    void askToClearFolder();
    void framingChanged();

    void paintSpine (juce::Graphics&);
    void paintLoudness (juce::Graphics&);
    void paintMeters (juce::Graphics&);
    void paintControls (juce::Graphics&);
    void paintPreview (juce::Graphics&);
    void paintFooterStatus (juce::Graphics&);
    void paintFocusOutline (juce::Graphics&, juce::Component& target);

    struct Status { juce::String text, detail; bool error = false; };
    Status currentStatus() const;

    ReelsCaptureProcessor& processor;
    c00kup::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    std::unique_ptr<juce::Drawable> symbol, wordmark;

    juce::TextButton clearButton, imageButton, recordButton, stopButton, openButton;
    juce::ToggleButton cameraToggle;
    juce::ComboBox backgroundBox, effectBox;
    juce::Slider zoomKnob, heightKnob, syncKnob;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Medidores (em dB)
    float peakDb[2] { -100.0f, -100.0f };
    float maxDb[2]  { -100.0f, -100.0f };
    bool clipped[2] { false, false };
    float momentaryLufs = -100.0f, shortTermLufs = -100.0f;

    int lastPreviewCounter = -1;
    bool draggingPreview = false;
    float dragStartX = 0.5f, dragStartY = 0.5f;
    juce::Rectangle<int> statusArea;

    // Área invisível por cima do texto de estado, para mostrar a mensagem completa como dica
    struct StatusHotspot : public juce::Component, public juce::SettableTooltipClient {};
    StatusHotspot statusHotspot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelsCaptureEditor)
};
