#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// Medidor de loudness segundo a ITU-R BS.1770 (o mesmo princípio do "LUFS"
// usado pelo Instagram/TikTok). Calcula:
//   - Momentary  (janela de 400 ms)
//   - Short-term (janela de 3 s)
// Funciona em blocos de 100 ms. Seguro para a thread de áudio (sem alocações em process()).
class LoudnessMeter
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        computeCoefficients();
        binLength = std::max (1, (int) std::lround (sr * 0.1));
        reset();
    }

    void reset()
    {
        for (auto& s : states) s = {};
        bins.fill (0.0);
        binIndex = 0;
        binsFilled = 0;
        currentBinSum = 0.0;
        currentBinCount = 0;
        momentary.store (-100.0f);
        shortTerm.store (-100.0f);
    }

    // channels: ponteiros para as amostras de cada canal (até 2 são usados).
    void process (const float* const* channels, int numChannels, int numSamples)
    {
        numChannels = std::min (numChannels, maxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            double sumSq = 0.0;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const double y = kWeight (states[(size_t) ch], (double) channels[ch][i]);
                sumSq += y * y;
            }

            currentBinSum += sumSq;

            if (++currentBinCount >= binLength)
                closeBin();
        }
    }

    std::atomic<float> momentary { -100.0f };
    std::atomic<float> shortTerm { -100.0f };

private:
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct ChannelState { double z1a = 0, z2a = 0, z1b = 0, z2b = 0; };

    static constexpr int numBins = 30; // 30 x 100 ms = 3 s

    void computeCoefficients()
    {
        const double pi = 3.14159265358979323846;

        // Estágio 1: filtro "shelf" de alta frequência (efeito da cabeça)
        {
            const double f0 = 1681.974450955533;
            const double G  = 3.999843853973347;
            const double Q  = 0.7071752369554196;
            const double K  = std::tan (pi * f0 / sr);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;

            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }

        // Estágio 2: passa-altos RLB
        {
            const double f0 = 38.13547087602444;
            const double Q  = 0.5003270373238773;
            const double K  = std::tan (pi * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;

            highpass.b0 = 1.0;
            highpass.b1 = -2.0;
            highpass.b2 = 1.0;
            highpass.a1 = 2.0 * (K * K - 1.0) / a0;
            highpass.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    double kWeight (ChannelState& s, double x) const
    {
        // Forma direta II transposta, dois estágios em série
        const double y1 = shelf.b0 * x + s.z1a;
        s.z1a = shelf.b1 * x - shelf.a1 * y1 + s.z2a;
        s.z2a = shelf.b2 * x - shelf.a2 * y1;

        const double y2 = highpass.b0 * y1 + s.z1b;
        s.z1b = highpass.b1 * y1 - highpass.a1 * y2 + s.z2b;
        s.z2b = highpass.b2 * y1 - highpass.a2 * y2;

        return y2;
    }

    void closeBin()
    {
        bins[(size_t) binIndex] = currentBinSum;
        binIndex = (binIndex + 1) % numBins;
        binsFilled = std::min (binsFilled + 1, numBins);
        currentBinSum = 0.0;
        currentBinCount = 0;

        momentary.store (loudnessOfLastBins (4));
        shortTerm.store (loudnessOfLastBins (numBins));
    }

    float loudnessOfLastBins (int count) const
    {
        count = std::min (count, binsFilled);
        if (count <= 0)
            return -100.0f;

        double sum = 0.0;
        for (int k = 1; k <= count; ++k)
            sum += bins[(size_t) ((binIndex - k + numBins) % numBins)];

        const double meanSquare = sum / ((double) count * (double) binLength);
        if (meanSquare <= 1.0e-12)
            return -100.0f;

        return (float) (-0.691 + 10.0 * std::log10 (meanSquare));
    }

    double sr = 48000.0;
    Biquad shelf, highpass;
    std::array<ChannelState, maxChannels> states {};
    std::array<double, numBins> bins {};
    int binIndex = 0, binsFilled = 0, binLength = 4800, currentBinCount = 0;
    double currentBinSum = 0.0;
};
