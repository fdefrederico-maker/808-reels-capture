#include "WavRecorder.h"

WavRecorder::WavRecorder() : juce::Thread ("C00KUP WAV writer") {}

WavRecorder::~WavRecorder()
{
    if (isRecording())
        stop();
}

void WavRecorder::prepare (double hostSampleRate)
{
    if (! isRecording())
        hostRate.store (hostSampleRate);
}

void WavRecorder::pushBlock (const float* const* channels, int numChannels, int numSamples)
{
    if (! recording.load (std::memory_order_acquire) || numSamples <= 0 || numChannels <= 0)
        return;

    audioThreadInside.store (true);

    if (recording.load())
    {
        if (! gotFirstBlock.load())
        {
            startTicks.store (juce::Time::getHighResolutionTicks());
            gotFirstBlock.store (true);
        }

        const auto scope = fifo.write (numSamples);

        if (scope.blockSize1 + scope.blockSize2 < numSamples)
            overflows.fetch_add (1);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = channels[juce::jmin (ch, numChannels - 1)];  // mono -> duplica

            if (scope.blockSize1 > 0)
                fifoBuffer.copyFrom (ch, scope.startIndex1, src, scope.blockSize1);
            if (scope.blockSize2 > 0)
                fifoBuffer.copyFrom (ch, scope.startIndex2, src + scope.blockSize1, scope.blockSize2);
        }
    }

    audioThreadInside.store (false);
}

bool WavRecorder::start (const juce::File& file, juce::String& error)
{
    if (isRecording())
        return true;

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
    {
        error = "Não foi possível criar o ficheiro WAV: " + file.getFullPathName();
        return false;
    }

    const auto now = juce::Time::getCurrentTime();
    const auto bwav = juce::WavAudioFormat::createBWAVMetadata ("C00KUP - master",
                                                               "C00KUP (808College)",
                                                               "C00KUP",
                                                               now, 0, "A=PCM,F=48000,W=24,M=stereo");
    std::unordered_map<juce::String, juce::String> metadata;
    for (auto& key : bwav.getAllKeys())
        metadata[key] = bwav[key];

    juce::WavAudioFormat wav;
    writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                              .withSampleRate (outputSampleRate)
                                              .withNumChannels (2)
                                              .withBitsPerSample (24)
                                              .withMetadataValues (metadata));
    if (writer == nullptr)
    {
        error = "Falha ao preparar o escritor WAV.";
        return false;
    }

    currentFile = file;
    ratio = hostRate.load() / outputSampleRate;

    for (int ch = 0; ch < 2; ++ch)
    {
        pending[ch].clear();
        pending[ch].reserve (1 << 16);
        resampled[ch].assign (1 << 16, 0.0f);
        interpolators[ch].reset();
    }

    fifo.reset();
    framesWritten.store (0);
    overflows.store (0);
    gotFirstBlock.store (false);
    startTicks.store (0);

    startThread();
    recording.store (true, std::memory_order_release);
    return true;
}

WavRecorder::TakeInfo WavRecorder::stop()
{
    TakeInfo info;

    if (! isRecording())
        return info;

    recording.store (false, std::memory_order_release);

    // Espera que a thread de áudio termine o bloco que possa estar a copiar
    for (int i = 0; i < 1000 && audioThreadInside.load(); ++i)
        juce::Thread::sleep (1);

    signalThreadShouldExit();
    notify();
    stopThread (10000);   // run() faz a última descarga antes de sair

    writer.reset();       // fecha o ficheiro (atualiza os cabeçalhos do WAV)

    info.file = currentFile;
    info.startTicks = startTicks.load();
    info.framesWritten = framesWritten.load();
    info.ok = gotFirstBlock.load() && info.framesWritten > 0;

    if (overflows.load() > 0)
    {
        DBG ("WavRecorder: FIFO overflow x" << overflows.load());
    }

    return info;
}

double WavRecorder::getSecondsRecorded() const
{
    return (double) framesWritten.load() / outputSampleRate;
}

void WavRecorder::run()
{
    while (! threadShouldExit())
    {
        drain (false);
        wait (20);
    }

    drain (true);
}

void WavRecorder::drain (bool final)
{
    // 1) Ler tudo o que está no FIFO para 'pending'
    const int available = fifo.getNumReady();

    if (available > 0)
    {
        const auto scope = fifo.read (available);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& p = pending[ch];
            const float* src = fifoBuffer.getReadPointer (ch);
            p.insert (p.end(), src + scope.startIndex1, src + scope.startIndex1 + scope.blockSize1);
            p.insert (p.end(), src + scope.startIndex2, src + scope.startIndex2 + scope.blockSize2);
        }
    }

    const int numPending = (int) pending[0].size();
    if (numPending == 0 || writer == nullptr)
        return;

    // 2) Sem conversão: escrever diretamente
    if (std::abs (ratio - 1.0) < 1.0e-9)
    {
        const float* chans[2] = { pending[0].data(), pending[1].data() };
        writer->writeFromFloatArrays (chans, 2, numPending);
        framesWritten.fetch_add (numPending);
        pending[0].clear();
        pending[1].clear();
        return;
    }

    // 3) Conversão de frequência (ex.: 44,1 kHz -> 48 kHz)
    const int margin = final ? 0 : 16;
    const int numOut = (int) std::floor ((double) (numPending - margin) / ratio);
    if (numOut <= 0)
        return;

    int used = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        if ((int) resampled[ch].size() < numOut)
            resampled[ch].resize ((size_t) numOut);

        used = interpolators[ch].process (ratio, pending[ch].data(), resampled[ch].data(), numOut,
                                          numPending, 0);
    }

    const float* chans[2] = { resampled[0].data(), resampled[1].data() };
    writer->writeFromFloatArrays (chans, 2, numOut);
    framesWritten.fetch_add (numOut);

    used = juce::jlimit (0, numPending, used);
    for (auto& p : pending)
        p.erase (p.begin(), p.begin() + used);
}
