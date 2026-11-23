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

    juce::TextButton recordButton, stopButton, exportButton;
    juce::Slider cropSlider;

    float momentaryLufs = -100.0f, shortTermLufs = -100.0f;
    int lastPreviewCounter = -1;
    juce::String lastStatus;

    juce::Rectangle<int> meterPanel, loudnessPanel, previewPanel, headerArea, statusArea, cropLabelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelsCaptureEditor)
};
