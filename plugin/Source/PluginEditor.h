#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "MeterComponent.h"

// Janela do plugin: medidores do master, loudness (LUFS), pré-visualização 9:16
// ao vivo (vinda do motor) e os botões Gravar / Parar / Exportar.
class ReelsCaptureEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit ReelsCaptureEditor (ReelsCaptureProcessor&);
    ~ReelsCaptureEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateButtons();
    juce::Rectangle<float> getPreviewFrame() const;

    ReelsCaptureProcessor& processor;

    reels::MeterScale scale;
    reels::ChannelMeter meterL { "L" }, meterR { "R" };

    juce::TextButton recordButton, stopButton, exportButton, clearButton;
    void askToClearFolder();
    juce::Slider cropSlider, zoomSlider, cropYSlider, syncSlider;
    juce::ToggleButton cameraToggle;
    juce::ComboBox backgroundBox, effectBox;
    juce::TextButton imageButton;
    std::unique_ptr<juce::FileChooser> fileChooser;

    void chooseBackgroundImage();
    void framingChanged();

    // Arrastar / roda do rato na pré-visualização
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool draggingPreview = false;
    float dragStartX = 0.5f, dragStartY = 0.5f;
    void syncVideoControls();

    float momentaryLufs = -100.0f, shortTermLufs = -100.0f;
    int lastPreviewCounter = -1;
    juce::String lastStatus;

    juce::Rectangle<int> meterPanel, loudnessPanel, previewPanel, headerArea, statusArea, cropLabelArea, videoArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelsCaptureEditor)
};
