#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace reels
{
// Paleta de cores partilhada pela interface
namespace colours
{
    const juce::Colour background   { 0xff111216 };
    const juce::Colour panel        { 0xff1b1d23 };
    const juce::Colour panelEdge    { 0xff2a2d36 };
    const juce::Colour text         { 0xffe8e8ee };
    const juce::Colour textDim      { 0xff8a8d99 };
    const juce::Colour accent       { 0xffff3b5c };   // vermelho "808"
    const juce::Colour meterGreen   { 0xff2ee59d };
    const juce::Colour meterYellow  { 0xffffd23f };
    const juce::Colour meterRed     { 0xffff3b5c };
}

inline float gainToDb (float gain)  { return juce::Decibels::gainToDecibels (gain, -100.0f); }

inline juce::String formatDb (float db)
{
    if (db <= -99.0f)
        return "-inf";
    return juce::String (db, 1);
}

//==============================================================================
// Um medidor vertical para um canal: barra de pico (clara), barra RMS (sólida),
// linha de pico máximo (hold) e indicador de clip. Clicar reinicia o clip/hold.
class ChannelMeter : public juce::Component
{
public:
    static constexpr float minDb = -60.0f;
    static constexpr float maxDb = 6.0f;

    explicit ChannelMeter (juce::String labelText) : label (std::move (labelText)) {}

    // Chamado ~30x por segundo com os valores mais recentes (em dB).
    void update (float newPeakDb, float newRmsDb, double nowMs)
    {
        // Queda suave do pico: ~24 dB por segundo
        const float fall = 24.0f / 30.0f;
        peakDb = juce::jmax (newPeakDb, peakDb - fall);
        rmsDb  = newRmsDb;

        if (newPeakDb >= holdDb || nowMs - holdTimeMs > 1500.0)
        {
            holdDb = newPeakDb;
            holdTimeMs = nowMs;
        }

        maxPeakDb = juce::jmax (maxPeakDb, newPeakDb);

        if (newPeakDb >= -0.01f)
            clipped = true;

        repaint();
    }

    float getMaxPeakDb() const { return maxPeakDb; }
    bool isClipped() const     { return clipped; }

    void resetHold()
    {
        clipped = false;
        maxPeakDb = -100.0f;
        holdDb = -100.0f;
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override { resetHold(); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();

        // Indicador de clip no topo
        auto clipArea = area.removeFromTop (10.0f);
        g.setColour (clipped ? colours::meterRed : colours::panelEdge);
        g.fillRoundedRectangle (clipArea, 2.0f);

        area.removeFromTop (4.0f);
        auto labelArea = area.removeFromBottom (16.0f);

        // Fundo da barra
        g.setColour (juce::Colour (0xff0b0c0f));
        g.fillRoundedRectangle (area, 3.0f);

        auto bar = area.reduced (2.0f);

        // Gradiente verde -> amarelo -> vermelho
        juce::ColourGradient grad (colours::meterRed, bar.getX(), dbToY (0.0f, bar),
                                   colours::meterGreen, bar.getX(), dbToY (minDb, bar), false);
        grad.addColour (normalisedPos (-6.0f),  colours::meterYellow);
        grad.addColour (normalisedPos (-18.0f), colours::meterGreen);

        // Pico (mais transparente)
        const float peakY = dbToY (peakDb, bar);
        g.setGradientFill (grad);
        g.setOpacity (0.35f);
        g.fillRect (bar.withTop (peakY));

        // RMS (sólido)
        const float rmsY = dbToY (rmsDb, bar);
        g.setOpacity (1.0f);
        g.fillRect (bar.withTop (rmsY));

        // Linha de "peak hold"
        if (holdDb > minDb)
        {
            const float holdY = dbToY (holdDb, bar);
            g.setColour (holdDb >= -0.01f ? colours::meterRed : colours::text);
            g.fillRect (bar.getX(), holdY - 1.0f, bar.getWidth(), 2.0f);
        }

        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (label, labelArea, juce::Justification::centred);
    }

    // Converte dB em coordenada vertical dentro de 'bar'
    static float dbToY (float db, juce::Rectangle<float> bar)
    {
        return bar.getBottom() - normalisedLevel (db) * bar.getHeight();
    }

    static float normalisedLevel (float db)
    {
        return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
    }

private:
    // Posição (0 = topo do gradiente em 0 dB, 1 = fundo em minDb)
    static double normalisedPos (float db) { return (double) ((0.0f - db) / (0.0f - minDb)); }

    juce::String label;
    float peakDb = -100.0f, rmsDb = -100.0f, holdDb = -100.0f, maxPeakDb = -100.0f;
    double holdTimeMs = 0.0;
    bool clipped = false;
};

//==============================================================================
// Escala em dB desenhada ao lado dos medidores
class MeterScale : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        area.removeFromTop (14.0f);
        area.removeFromBottom (16.0f);
        auto bar = area.reduced (0.0f, 2.0f);

        g.setFont (juce::FontOptions (10.0f));

        for (int db : { 6, 0, -3, -6, -12, -18, -24, -36, -48, -60 })
        {
            const float y = ChannelMeter::dbToY ((float) db, bar);
            g.setColour (db == 0 ? colours::text : colours::textDim);
            g.drawText (db > 0 ? "+" + juce::String (db) : juce::String (db),
                        juce::Rectangle<float> (0.0f, y - 6.0f, area.getWidth() - 6.0f, 12.0f),
                        juce::Justification::centredRight);
            g.fillRect (area.getRight() - 4.0f, y, 4.0f, 1.0f);
        }
    }
};
} // namespace reels
