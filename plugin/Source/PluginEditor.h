#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "MeterComponent.h"

// Janela do plugin: medidores do master, loudness (LUFS), área reservada para a
// pré-visualização 9:16 e os botões Gravar / Parar / Exportar (ativos nas próximas fases).
class ReelsCaptureEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit ReelsCaptureEditor (ReelsCaptureProcessor&);
    ~ReelsCaptureEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ReelsCaptureProcessor& processor;

    reels::MeterScale scale;
    reels::ChannelMeter meterL { "L" }, meterR { "R" };

    juce::TextButton recordButton, stopButton, exportButton;

    float momentaryLufs = -100.0f, shortTermLufs = -100.0f;

    juce::Rectangle<int> meterPanel, loudnessPanel, previewPanel, headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelsCaptureEditor)
};
