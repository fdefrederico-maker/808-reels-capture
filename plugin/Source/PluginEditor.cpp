#include "PluginEditor.h"

using namespace reels;

namespace
{
    // Texto com acentos: o código-fonte está em UTF-8.
    juce::String pt (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    constexpr int editorWidth  = 720;
    constexpr int editorHeight = 500;
}

ReelsCaptureEditor::ReelsCaptureEditor (ReelsCaptureProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    addAndMakeVisible (scale);
    addAndMakeVisible (meterL);
    addAndMakeVisible (meterR);

    auto setupButton = [this] (juce::TextButton& b, const juce::String& text, juce::Colour colour)
    {
        b.setButtonText (text);
        b.setColour (juce::TextButton::buttonColourId, colours::panel);
        b.setColour (juce::TextButton::buttonOnColourId, colour);
        b.setColour (juce::TextButton::textColourOffId, colours::text);
        b.setTooltip (pt ("Disponível na Fase 2"));
        b.setEnabled (false);   // Fase 1: só medidores
        addAndMakeVisible (b);
    };

    setupButton (recordButton, pt ("\xe2\x97\x8f  Gravar"), colours::accent);       // ●
    setupButton (stopButton,   pt ("\xe2\x96\xa0  Parar"),  colours::textDim);      // ■
    setupButton (exportButton, pt ("Exportar"),             colours::meterGreen);

    setSize (editorWidth, editorHeight);
    startTimerHz (30);
}

void ReelsCaptureEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const int numCh = processor.getNumMeterChannels();

    const float peakL = gainToDb (processor.takePeak (0));
    const float rmsL  = gainToDb (processor.getRms (0));
    meterL.update (peakL, rmsL, now);

    if (numCh > 1)
        meterR.update (gainToDb (processor.takePeak (1)), gainToDb (processor.getRms (1)), now);
    else
        meterR.update (peakL, rmsL, now);   // mono: R mostra o mesmo que L

    momentaryLufs = processor.loudness.momentary.load();
    shortTermLufs = processor.loudness.shortTerm.load();

    repaint (loudnessPanel);
    repaint (headerArea);
}

void ReelsCaptureEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Cabeçalho
    {
        auto h = headerArea;
        g.setColour (colours::accent);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText ("808", h.removeFromLeft (46), juce::Justification::centredLeft);

        g.setColour (colours::text);
        g.drawText ("REELS CAPTURE", h.removeFromLeft (220), juce::Justification::centredLeft);

        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        const auto sr = processor.getCurrentSampleRate();
        juce::String info = juce::String (sr / 1000.0, 1) + " kHz";
        if (std::abs (sr - 48000.0) > 1.0)
            info << pt ("  \xc2\xb7  (o WAV final será 48 kHz)");
        g.drawText (info, h, juce::Justification::centredRight);
    }

    // Painéis
    for (auto r : { meterPanel, loudnessPanel, previewPanel })
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    // Títulos dos painéis
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (colours::textDim);
    g.drawText ("MASTER", meterPanel.reduced (12, 8).removeFromTop (14), juce::Justification::centredLeft);
    g.drawText ("LOUDNESS", loudnessPanel.reduced (12, 8).removeFromTop (14), juce::Justification::centredLeft);

    // Valores de pico máximo por canal (desde o último clique no medidor)
    {
        auto row = meterPanel.reduced (12, 8).removeFromBottom (18);
        g.setFont (juce::FontOptions (11.0f));
        auto drawMax = [&] (const ChannelMeter& m, juce::Rectangle<int> r)
        {
            g.setColour (m.isClipped() ? colours::meterRed : colours::text);
            g.drawText ("max " + formatDb (m.getMaxPeakDb()), r, juce::Justification::centred);
        };
        drawMax (meterL, row.withX (meterL.getX() - 14).withWidth (meterL.getWidth() + 28));
        drawMax (meterR, row.withX (meterR.getX() - 14).withWidth (meterR.getWidth() + 28));
    }

    // Loudness (LUFS)
    {
        auto area = loudnessPanel.reduced (12, 8);
        area.removeFromTop (20);

        auto drawValue = [&] (const juce::String& title, float lufs, juce::Rectangle<int> r)
        {
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (title, r.removeFromTop (16), juce::Justification::centredLeft);

            const bool hot = lufs > -9.0f;
            g.setColour (lufs <= -99.0f ? colours::textDim : (hot ? colours::meterYellow : colours::text));
            g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
            auto valueRow = r.removeFromTop (34);
            g.drawText (formatDb (lufs), valueRow.removeFromLeft (84), juce::Justification::centredRight);
            valueRow.removeFromLeft (8);
            g.setFont (juce::FontOptions (12.0f));
            g.setColour (colours::textDim);
            g.drawText ("LUFS", valueRow, juce::Justification::centredLeft);
        };

        drawValue ("Momentary (400 ms)", momentaryLufs, area.removeFromTop (54));
        area.removeFromTop (6);
        drawValue ("Short-term (3 s)", shortTermLufs, area.removeFromTop (54));

        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (pt ("Referência para Reels: cerca de -14 LUFS.\nClique num medidor para limpar o clip."),
                          area.removeFromBottom (32), juce::Justification::bottomLeft, 2);
    }

    // Pré-visualização 9:16 (reservado)
    {
        auto inner = previewPanel.reduced (12);
        auto frame = juce::Rectangle<float> (0, 0, 9.0f, 16.0f)
                         .transformedBy (juce::AffineTransform::scale ((float) inner.getHeight() / 16.0f))
                         .withCentre (inner.toFloat().getCentre());

        g.setColour (juce::Colour (0xff0b0c0f));
        g.fillRoundedRectangle (frame, 6.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (frame, 6.0f, 1.0f);

        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (pt ("Pré-visualização 9:16\n(Fase 3)"), frame.toNearestInt(),
                          juce::Justification::centred, 2);
    }
}

void ReelsCaptureEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    headerArea = area.removeFromTop (32);
    area.removeFromTop (12);

    auto buttons = area.removeFromBottom (44);
    area.removeFromBottom (12);

    // Botões
    {
        const int gap = 10;
        const int w = (buttons.getWidth() - 2 * gap) / 3;
        recordButton.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (gap);
        stopButton.setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (gap);
        exportButton.setBounds (buttons);
    }

    // Pré-visualização à direita (proporção 9:16 + margens)
    const int previewWidth = (int) std::round ((area.getHeight() - 24) * 9.0 / 16.0) + 24;
    previewPanel = area.removeFromRight (previewWidth);
    area.removeFromRight (12);

    meterPanel = area.removeFromLeft (170);
    area.removeFromLeft (12);
    loudnessPanel = area;

    // Medidores dentro do painel MASTER
    auto m = meterPanel.reduced (12, 8);
    m.removeFromTop (20);
    m.removeFromBottom (22);
    scale.setBounds (m.removeFromLeft (34));
    m.removeFromLeft (6);
    const int meterWidth = (m.getWidth() - 10) / 2;
    meterL.setBounds (m.removeFromLeft (meterWidth));
    m.removeFromLeft (10);
    meterR.setBounds (m.removeFromLeft (meterWidth));
}
