#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LoudnessMeter.h"

// Processador do plugin. Nesta Fase 1 deixa o áudio passar sem alterações
// e apenas mede os níveis do master para mostrar no ecrã.
class ReelsCaptureProcessor : public juce::AudioProcessor
{
public:
    ReelsCaptureProcessor();
    ~ReelsCaptureProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    //==============================================================================
    // Valores lidos pelo editor (ecrã). Todos em amplitude linear (0..1+).
    struct ChannelLevels
    {
        std::atomic<float> peak { 0.0f };  // pico máximo desde a última leitura
        std::atomic<float> rms  { 0.0f };  // RMS suavizado (~300 ms)
    };

    static constexpr int maxMeterChannels = 2;

    // Devolve e limpa o pico acumulado (chamado pelo editor ~30x por segundo).
    float takePeak (int channel)       { return levels[(size_t) channel].peak.exchange (0.0f); }
    float getRms (int channel) const   { return levels[(size_t) channel].rms.load(); }
    int   getNumMeterChannels() const  { return numMeterChannels.load(); }
    double getCurrentSampleRate() const { return currentSampleRate.load(); }

    LoudnessMeter loudness;

private:
    std::array<ChannelLevels, maxMeterChannels> levels;
    std::array<double, maxMeterChannels> rmsMeanSquare {};
    std::atomic<int> numMeterChannels { 2 };
    std::atomic<double> currentSampleRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelsCaptureProcessor)
};
