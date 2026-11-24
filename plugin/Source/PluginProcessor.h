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
    void setFraming (float x, float y, float zoom);   // recorte horizontal, vertical e zoom do FL

    // Câmara e fundo (ids do menu "Fundo")
    enum BackgroundId { bgOriginal = 1, bgBlur, bgImage, bgBlack, bgWhite, bgGrey, bgGreen };
    void setCameraEnabled (bool shouldBeOn);
    void setBackground (int backgroundId);
    void setBackgroundImage (const juce::File& image);
    // Efeito retro (ids do menu "Efeito")
    enum EffectId { fxNone = 1, fxVhs, fxFilm, fxGlitch, fxTv };
    void setEffect (int effectId);
    int getEffectId() const                { return effectId; }

    bool isCameraEnabled() const           { return cameraEnabled; }
    int getBackgroundId() const            { return backgroundId; }
    juce::File getBackgroundImage() const  { return backgroundImage; }

    SessionState getSessionState() const   { return sessionState; }
    double getSecondsRecorded() const      { return wavRecorder.getSecondsRecorded(); }
    float getCropPosition() const          { return cropPosition; }
    float getCropY() const                 { return cropY; }
    float getZoom() const                  { return flZoom; }

    // Ajuste fino da sincronização (ms): positivo atrasa o som em relação à imagem
    void setSyncOffsetMs (float ms)        { syncOffsetMs = juce::jlimit (-300.0f, 500.0f, ms); }
    float getSyncOffsetMs() const          { return syncOffsetMs; }

    // Exportar: volta a montar o último vídeo com o ajuste atual e mostra-o no Finder
    void exportAgain();
    float getExportProgress() const        { return exportProgress; }
    const juce::String& getStatusText() const { return statusText; }
    bool hasLastExport() const             { return lastExport.existsAsFile(); }

    EngineLink& getEngineLink()            { return engineLink; }

    static juce::File getOutputFolder();

    // "Limpar pasta": apaga DEFINITIVAMENTE tudo o que está na pasta de saída
    struct FolderUsage { int files = 0; juce::int64 bytes = 0; };
    static FolderUsage measureOutputFolder();
    bool canClearOutputFolder() const      { return sessionState == SessionState::idle; }
    void clearOutputFolder();

private:
    void handleEngineEvent (const juce::var& ev);
    void sendCrop();
    void sendVideoSettings();
    void sendBassLevel();

    WavRecorder wavRecorder;
    EngineLink engineLink;

    SessionState sessionState = SessionState::idle;
    bool editorOpen = false;
    bool videoActive = false;
    float cropPosition = 0.5f;
    float cropY = 0.5f;
    float flZoom = 1.0f;
    float syncOffsetMs = 0.0f;
    bool revealWhenExported = false;
    std::atomic<double> blockSeconds { 0.01 };   // duração de um bloco de áudio (latência de saída)

    juce::int64 heardTicks (juce::int64 processedTicks) const;
    bool cameraEnabled = true;
    int effectId = fxNone;

    // Graves (kick/808) para o efeito "Glitch ao ritmo"
    std::atomic<float> bassPeak { 0.0f };
    float bassLowpass = 0.0f, bassCoeff = 0.02f;
    juce::TimedCallback levelTimer { [this] { sendBassLevel(); } };
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
