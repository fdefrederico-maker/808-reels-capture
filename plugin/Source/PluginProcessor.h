#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LoudnessMeter.h"
#include "WavRecorder.h"
#include "EngineLink.h"

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

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

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

    //==============================================================================
    // Gravação (chamar apenas da thread de mensagens / interface)
    enum class SessionState { idle, recording, exporting };

    void editorVisibilityChanged (bool editorIsOpen);
    void startRecording();
    void stopRecording();
    void revealLastExport();
    void setCropPosition (float x);

    // Câmara e fundo (ids do menu "Fundo")
    enum BackgroundId { bgOriginal = 1, bgBlur, bgImage, bgBlack, bgWhite, bgGrey, bgGreen };
    void setCameraEnabled (bool shouldBeOn);
    void setBackground (int backgroundId);
    void setBackgroundImage (const juce::File& image);
    bool isCameraEnabled() const           { return cameraEnabled; }
    int getBackgroundId() const            { return backgroundId; }
    juce::File getBackgroundImage() const  { return backgroundImage; }

    SessionState getSessionState() const   { return sessionState; }
    double getSecondsRecorded() const      { return wavRecorder.getSecondsRecorded(); }
    float getCropPosition() const          { return cropPosition; }
    float getExportProgress() const        { return exportProgress; }
    const juce::String& getStatusText() const { return statusText; }
    bool hasLastExport() const             { return lastExport.existsAsFile(); }

    EngineLink& getEngineLink()            { return engineLink; }

    static juce::File getOutputFolder();

private:
    void handleEngineEvent (const juce::var& ev);
    void sendCrop();
    void sendVideoSettings();

    WavRecorder wavRecorder;
    EngineLink engineLink;

    SessionState sessionState = SessionState::idle;
    bool editorOpen = false;
    bool videoActive = false;
    float cropPosition = 0.5f;
    bool cameraEnabled = true;
    int backgroundId = bgOriginal;
    juce::File backgroundImage;
    float exportProgress = 0.0f;
    juce::String statusText;
    juce::String currentTakeName;
    juce::File lastExport;

    std::array<ChannelLevels, maxMeterChannels> levels;
    std::array<double, maxMeterChannels> rmsMeanSquare {};
    std::atomic<int> numMeterChannels { 2 };
    std::atomic<double> currentSampleRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelsCaptureProcessor)
};
