#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <vector>

// Grava o master num WAV estéreo de 48 kHz / 24 bits.
//
// - A thread de áudio só copia amostras para um FIFO (sem alocações, sem disco).
// - Uma thread de fundo lê o FIFO, converte para 48 kHz se necessário e escreve no disco.
// - A "marca de tempo" é o relógio do sistema (mach_absolute_time) no início do
//   primeiro bloco gravado. O motor de vídeo usa o mesmo relógio, o que permite
//   sincronizar áudio e imagem na exportação.
class WavRecorder : private juce::Thread
{
public:
    static constexpr double outputSampleRate = 48000.0;

    struct TakeInfo
    {
        juce::File file;
        juce::int64 startTicks = 0;      // mach_absolute_time do 1.º bloco gravado
        juce::int64 framesWritten = 0;   // amostras por canal escritas (a 48 kHz)
        bool ok = false;
    };

    WavRecorder();
    ~WavRecorder() override;

    // Thread de áudio
    void prepare (double hostSampleRate);
    void pushBlock (const float* const* channels, int numChannels, int numSamples);

    // Thread de mensagens (interface)
    bool start (const juce::File& file, juce::String& error);
    TakeInfo stop();
    bool isRecording() const { return recording.load(); }
    double getSecondsRecorded() const;

private:
    void run() override;
    void drain (bool final);

    static constexpr int fifoCapacity = 1 << 20;  // ~21 s a 48 kHz

    juce::AbstractFifo fifo { fifoCapacity };
    juce::AudioBuffer<float> fifoBuffer { 2, fifoCapacity };

    std::atomic<bool> recording { false };
    std::atomic<bool> audioThreadInside { false };
    std::atomic<bool> gotFirstBlock { false };
    std::atomic<juce::int64> startTicks { 0 };
    std::atomic<double> hostRate { 48000.0 };
    std::atomic<juce::int64> framesWritten { 0 };
    std::atomic<int> overflows { 0 };

    // Só usados pela thread de fundo
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::File currentFile;
    double ratio = 1.0;
    std::vector<float> pending[2];
    std::vector<float> resampled[2];
    juce::WindowedSincInterpolator interpolators[2];

    JUCE_DECLARE_NON_COPYABLE (WavRecorder)
};
