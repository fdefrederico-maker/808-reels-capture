#include "PluginProcessor.h"
#include "PluginEditor.h"

ReelsCaptureProcessor::ReelsCaptureProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool ReelsCaptureProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // Efeito de passagem: entrada igual à saída.
    return layouts.getMainInputChannelSet() == out;
}

void ReelsCaptureProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate.store (sampleRate);
    numMeterChannels.store (juce::jlimit (1, maxMeterChannels, getTotalNumInputChannels()));
    rmsMeanSquare.fill (0.0);

    for (auto& l : levels)
    {
        l.peak.store (0.0f);
        l.rms.store (0.0f);
    }

    loudness.prepare (sampleRate);
}

void ReelsCaptureProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();

    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples == 0)
        return;

    const int meterChannels = juce::jmin (numIn, maxMeterChannels);
    const double sr = currentSampleRate.load();
    const double alpha = 1.0 - std::exp (-(double) numSamples / (0.3 * sr)); // constante de tempo 300 ms

    for (int ch = 0; ch < meterChannels; ++ch)
    {
        const float* data = buffer.getReadPointer (ch);

        float blockPeak = 0.0f;
        double sumSq = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const float s = data[i];
            blockPeak = juce::jmax (blockPeak, std::abs (s));
            sumSq += (double) s * (double) s;
        }

        // Máximo atómico: o editor "consome" o pico com takePeak().
        auto& peak = levels[(size_t) ch].peak;
        float prev = peak.load();
        while (blockPeak > prev && ! peak.compare_exchange_weak (prev, blockPeak)) {}

        auto& ms = rmsMeanSquare[(size_t) ch];
        ms += alpha * (sumSq / numSamples - ms);
        levels[(size_t) ch].rms.store ((float) std::sqrt (ms));
    }

    loudness.process (buffer.getArrayOfReadPointers(), meterChannels, numSamples);

    // O áudio não é alterado: a entrada passa tal e qual para a saída.
}

juce::AudioProcessorEditor* ReelsCaptureProcessor::createEditor()
{
    return new ReelsCaptureEditor (*this);
}

// Ponto de entrada exigido pelo JUCE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReelsCaptureProcessor();
}
