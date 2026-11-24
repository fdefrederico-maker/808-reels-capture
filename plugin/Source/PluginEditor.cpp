#include "PluginEditor.h"

using namespace c00kup;

namespace
{
    // Texto com acentos: o código-fonte está em UTF-8.
    juce::String pt (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    const juce::String minus = juce::String::fromUTF8 ("\xe2\x88\x92");   // − (sinal de menos tipográfico)
    const juce::String dot   = juce::String::fromUTF8 ("\xc2\xb7");       // ·
    const juce::String times = juce::String::fromUTF8 ("\xc3\x97");       // ×

    constexpr int editorWidth  = 760;
    constexpr int editorHeight = 600;

    // Medidas da SPEC (px a 1x)
    const juce::Rectangle<float> spineArea     { 0.0f, 0.0f, 76.0f, 600.0f };
    const juce::Rectangle<float> symbolArea    { 17.9f, 22.0f, 40.2f, 40.0f };
    const juce::Point<float>     wordmarkCentre{ 38.0f, 334.75f };   // medido sobre a referência @2x
    constexpr float              wordmarkLength = 303.5f;

    constexpr float mainLeft = 108.0f, mainRight = 728.0f;
    constexpr float controlsRight = 536.0f;

    const juce::Rectangle<float> meterBar[2]   { { 132.0f, 142.1f, 512.0f, 4.0f }, { 132.0f, 163.1f, 512.0f, 4.0f } };
    const juce::Rectangle<float> meterMax[2]   { { 656.0f, 138.6f, 72.0f, 11.0f }, { 656.0f, 159.6f, 72.0f, 11.0f } };
    const float                  rowLines[]    { 188.6f, 229.6f, 270.6f, 311.6f };
    const juce::Rectangle<float> ledArea       { 483.0f, 206.1f, 7.0f, 7.0f };
    const juce::Rectangle<float> previewArea   { 568.0f, 188.6f, 160.0f, 284.0f };
    const float                  knobColumnX[] { 112.0f, 280.0f, 448.0f };
    constexpr float              footerLineY = 511.0f;

    constexpr float meterMinDb = -54.0f, meterMaxDb = 6.0f;   // 0 dB fica a 90% da barra (SPEC)

    float gainToDb (float gain) { return juce::Decibels::gainToDecibels (gain, -100.0f); }

    juce::String signedNumber (float value, int decimals)
    {
        const auto text = juce::String (std::abs (value), decimals);
        return value < 0.0f && text != juce::String (0.0f, decimals) ? minus + text : text;
    }

    juce::String formatTimecode (double seconds)
    {
        const int s = (int) seconds;
        return juce::String (s / 3600).paddedLeft ('0', 2) + ":" + juce::String ((s / 60) % 60).paddedLeft ('0', 2)
               + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }

}

//==============================================================================
ReelsCaptureEditor::ReelsCaptureEditor (ReelsCaptureProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    symbol = createSymbol();
    wordmark = createWordmark();

    auto setupButton = [this] (juce::Button& b, const juce::String& styleName, const juce::String& text,
                               const juce::String& tip)
    {
        b.getProperties().set (style::key, styleName);
        b.setButtonText (text);
        b.setTooltip (tip);
        b.setMouseClickGrabsKeyboardFocus (false);   // contorno de foco só com o teclado
        b.setWantsKeyboardFocus (true);
        b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible (b);
    };

    setupButton (clearButton, style::square, "LIMPAR PASTA",
                 pt ("Apaga de vez todos os vídeos e áudios gravados (Filmes > C00KUP)"));
    setupButton (imageButton, style::square, "IMAGEM", pt ("Escolher a imagem de fundo (JPG, PNG, HEIC)"));
    setupButton (recordButton, style::pillRecord, "GRAVAR", pt ("Começa a gravar o master, a câmara e o FL"));
    setupButton (stopButton, style::pillStop, "PARAR", pt ("Para e exporta o MP4 automaticamente"));
    setupButton (openButton, style::link, "ABRIR PASTA", pt ("Abre no Finder a pasta dos vídeos"));
    setupButton (cameraToggle, style::toggleSwitch, {}, pt ("Câmara no topo do vídeo"));

    clearButton.onClick  = [this] { askToClearFolder(); };
    imageButton.onClick  = [this] { chooseBackgroundImage(); };
    recordButton.onClick = [this]
    {
        if (processor.getSessionState() == ReelsCaptureProcessor::SessionState::idle)
            processor.startRecording();
        updateButtons();
    };
    stopButton.onClick   = [this] { processor.stopRecording();  updateButtons(); };
    openButton.onClick   = [this] { processor.revealLastExport(); };
    cameraToggle.setClickingTogglesState (true);
    cameraToggle.onClick = [this] { processor.setCameraEnabled (cameraToggle.getToggleState()); syncVideoControls(); };

    // Campos rebaixados
    using P = ReelsCaptureProcessor;
    backgroundBox.addItem ("Sala original",  P::bgOriginal);
    backgroundBox.addItem ("Sala desfocada", P::bgBlur);
    backgroundBox.addItem (pt ("Imagem…"),   P::bgImage);
    backgroundBox.addSeparator();
    backgroundBox.addItem ("Preto",          P::bgBlack);
    backgroundBox.addItem ("Branco",         P::bgWhite);
    backgroundBox.addItem ("Cinzento",       P::bgGrey);
    backgroundBox.addItem ("Verde suave",    P::bgGreen);
    backgroundBox.setTooltip (pt ("O que aparece atrás de si na câmara"));
    backgroundBox.onChange = [this]
    {
        const int id = backgroundBox.getSelectedId();
        if (id == ReelsCaptureProcessor::bgImage && ! processor.getBackgroundImage().existsAsFile())
            chooseBackgroundImage();
        else
            processor.setBackground (id);
        syncVideoControls();
    };

    effectBox.addItem ("Nenhum",          P::fxNone);
    effectBox.addItem ("VHS",             P::fxVhs);
    effectBox.addItem ("Filme 8mm",       P::fxFilm);
    effectBox.addItem ("Glitch ao ritmo", P::fxGlitch);
    effectBox.addItem ("TV antiga (P&B)", P::fxTv);
    effectBox.setTooltip (pt ("Efeito retro aplicado a todo o vídeo"));
    effectBox.onChange = [this] { processor.setEffect (effectBox.getSelectedId()); };

    for (auto* box : { &backgroundBox, &effectBox })
    {
        box->setMouseClickGrabsKeyboardFocus (false);
        box->setWantsKeyboardFocus (true);
        box->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible (*box);
    }

    // Knobs: arrastar na vertical, duplo clique repõe, roda do rato
    auto setupKnob = [this] (juce::Slider& k, bool bipolar, double min, double max, double step,
                             double defaultValue, const juce::String& tip)
    {
        k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k.setRange (min, max, step);
        k.setDoubleClickReturnValue (true, defaultValue);
        k.setMouseDragSensitivity (180);
        k.getProperties().set (style::bipolar, bipolar);
        k.setTooltip (tip);
        k.setMouseClickGrabsKeyboardFocus (false);
        k.setWantsKeyboardFocus (true);
        k.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        addAndMakeVisible (k);
    };

    setupKnob (zoomKnob, false, 1.0, 4.0, 0.0, 1.0, pt ("Zoom da janela do FL (duplo clique = 1×)"));
    zoomKnob.setSkewFactorFromMidPoint (2.0);
    setupKnob (heightKnob, true, -50.0, 50.0, 1.0, 0.0, pt ("Que parte da janela do FL aparece: + sobe, − desce (duplo clique = centro)"));
    setupKnob (syncKnob, true, -500.0, 500.0, 10.0, 0.0,
               pt ("Opcional: a sincronização já é automática. Se a imagem parecer atrasada, rode para a direita."));

    zoomKnob.onValueChange   = [this] { framingChanged(); };
    heightKnob.onValueChange = [this] { framingChanged(); };
    syncKnob.onValueChange   = [this] { processor.setSyncOffsetMs ((float) syncKnob.getValue()); repaint(); };

    zoomKnob.setValue (processor.getZoom(), juce::dontSendNotification);
    heightKnob.setValue (std::round ((0.5 - processor.getCropY()) * 100.0), juce::dontSendNotification);
    syncKnob.setValue (processor.getSyncOffsetMs(), juce::dontSendNotification);

    syncVideoControls();

    statusHotspot.setInterceptsMouseClicks (true, false);
    addAndMakeVisible (statusHotspot);

    // Esticar pelo canto (ou pela janela do FL), mantendo a proporção 760:600
    setResizable (true, true);
    setResizeLimits (editorWidth, editorHeight, editorWidth * 2, editorHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) editorWidth / (double) editorHeight);

    const float scale = processor.getEditorScale();
    setSize (juce::roundToInt (editorWidth * scale), juce::roundToInt (editorHeight * scale));

    juce::Desktop::getInstance().addFocusChangeListener (this);
    processor.editorVisibilityChanged (true);
    updateButtons();
    startTimerHz (30);
}

ReelsCaptureEditor::~ReelsCaptureEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    processor.editorVisibilityChanged (false);
    setLookAndFeel (nullptr);
}

//==============================================================================
void ReelsCaptureEditor::resized()
{
    clearButton.setBounds (606, 31, 122, 28);

    cameraToggle.setBounds (498, 200, 38, 21);
    backgroundBox.setBounds (190, 236, 257, 31);
    imageButton.setBounds (456, 236, 80, 30);
    effectBox.setBounds (190, 277, 346, 31);

    zoomKnob.setBounds   (130, 327, 48, 48);
    heightKnob.setBounds (298, 327, 48, 48);
    syncKnob.setBounds   (466, 327, 48, 48);

    layoutFooter();

    // Os componentes ficam posicionados em coordenadas de 760×600 e são ampliados por igual
    const float scale = uiScale();
    for (auto* child : getChildren())
    {
        if (dynamic_cast<juce::ResizableCornerComponent*> (child) != nullptr
            || dynamic_cast<juce::TooltipWindow*> (child) != nullptr)
            continue;
        child->setTransform (juce::AffineTransform::scale (scale));
    }

    processor.setEditorScale (scale);
    repaint();
}

void ReelsCaptureEditor::layoutFooter()
{
    // Pílulas com largura ajustada ao texto, 14 px entre elas
    float x = mainLeft;
    const int y = 526, h = 52;

    const int recordWidth = juce::roundToInt (c00kup::LookAndFeel::pillWidth (style::pillRecord, recordButton.getButtonText()));
    recordButton.setBounds ((int) x, y, recordWidth, h);
    x += (float) recordWidth + 14.0f;

    const int stopWidth = juce::roundToInt (c00kup::LookAndFeel::pillWidth (style::pillStop, stopButton.getButtonText()));
    stopButton.setBounds ((int) x, y, stopWidth, h);
    x += (float) stopWidth + 14.0f;

    const int openWidth = juce::roundToInt (c00kup::LookAndFeel::linkWidth (openButton.getButtonText()));
    openButton.setBounds ((int) x, y, openWidth, h);

    const int statusLeft = openButton.getRight() + 12;
    statusArea = { statusLeft, 558, (int) mainRight - statusLeft, 20 };
    statusHotspot.setBounds (statusArea);
}

//==============================================================================
void ReelsCaptureEditor::syncVideoControls()
{
    const bool camera = processor.isCameraEnabled();
    cameraToggle.setToggleState (camera, juce::dontSendNotification);
    backgroundBox.setSelectedId (processor.getBackgroundId(), juce::dontSendNotification);
    effectBox.setSelectedId (processor.getEffectId(), juce::dontSendNotification);
    backgroundBox.setEnabled (camera);
    imageButton.setEnabled (camera);
    imageButton.setAlpha (camera ? 1.0f : 0.4f);

    const auto image = processor.getBackgroundImage();
    backgroundBox.setTooltip (processor.getBackgroundId() == ReelsCaptureProcessor::bgImage && image.existsAsFile()
                                  ? pt ("Imagem de fundo: ") + image.getFileName()
                                  : pt ("O que aparece atrás de si na câmara"));
    repaint();
}

void ReelsCaptureEditor::framingChanged()
{
    processor.setFraming (processor.getCropPosition(),
                          (float) (0.5 - heightKnob.getValue() / 100.0),
                          (float) zoomKnob.getValue());
    repaint();
}

void ReelsCaptureEditor::updateButtons()
{
    using S = ReelsCaptureProcessor::SessionState;
    const auto state = processor.getSessionState();
    const bool recording = state == S::recording;

    // A gravar, a pílula continua acesa (com o disco a pulsar); clicar não faz nada
    recordButton.setEnabled (state != S::exporting);
    stopButton.setEnabled (recording);
    clearButton.setEnabled (processor.canClearOutputFolder());
    clearButton.setAlpha (clearButton.isEnabled() ? 1.0f : 0.4f);

    const auto recordText = recording ? juce::String ("A GRAVAR") : juce::String ("GRAVAR");
    recordButton.getProperties().set (style::recording, recording);
    if (recordButton.getButtonText() != recordText)
    {
        recordButton.setButtonText (recordText);
        layoutFooter();
        repaint();
    }

    if (recording)
    {
        // Pulsação a 1 Hz
        const double phase = std::fmod (juce::Time::getMillisecondCounterHiRes() / 1000.0, 1.0);
        recordButton.getProperties().set (style::pulse, 0.5 + 0.5 * std::cos (phase * juce::MathConstants<double>::twoPi));
        recordButton.repaint();
    }
}

//==============================================================================
void ReelsCaptureEditor::timerCallback()
{
    // Medidores: pico com queda suave (~24 dB/s) e máximo desde o último clique
    const int numCh = processor.getNumMeterChannels();
    float newPeak[2];
    newPeak[0] = gainToDb (processor.takePeak (0));
    newPeak[1] = numCh > 1 ? gainToDb (processor.takePeak (1)) : newPeak[0];

    for (int ch = 0; ch < 2; ++ch)
    {
        peakDb[ch] = juce::jmax (newPeak[ch], peakDb[ch] - 24.0f / 30.0f);
        maxDb[ch] = juce::jmax (maxDb[ch], newPeak[ch]);
        if (newPeak[ch] >= -0.01f)
            clipped[ch] = true;
    }

    momentaryLufs = processor.loudness.momentary.load();
    shortTermLufs = processor.loudness.shortTerm.load();

    updateButtons();

    auto& link = processor.getEngineLink();
    if (link.previewCounter != lastPreviewCounter)
        lastPreviewCounter = link.previewCounter;

    const auto status = currentStatus();
    statusHotspot.setTooltip (status.detail.isNotEmpty() && status.detail != status.text ? status.detail : juce::String());

    repaint();
}

void ReelsCaptureEditor::globalFocusChanged (juce::Component*)
{
    repaint();   // contorno de foco
}

//==============================================================================
ReelsCaptureEditor::Status ReelsCaptureEditor::currentStatus() const
{
    using S = ReelsCaptureProcessor::SessionState;
    auto& link = const_cast<ReelsCaptureProcessor&> (processor).getEngineLink();
    const auto state = processor.getSessionState();

    if (state == S::recording)
    {
        if (processor.isVideoActive())
            return { "A GRAVAR " + dot + " 1080" + times + "1920", {}, false };
        return { pt ("A GRAVAR SÓ ÁUDIO (SEM MOTOR)"), link.engineMessage, true };
    }

    if (state == S::exporting)
        return { "A EXPORTAR " + juce::String (juce::roundToInt (processor.getExportProgress() * 100.0f)) + "%", {}, false };

    if (processor.getStatusText().isNotEmpty())
        return { processor.getStatusText().toUpperCase(), processor.getStatusDetail(), processor.isStatusError() };

    if (link.engineMissing)
        return { pt ("MOTOR NÃO INSTALADO"), link.engineMessage, true };
    if (! link.isConnected())
        return { "A ABRIR O MOTOR", link.engineMessage, false };
    if (! link.engineCapturing)
        return { "SEM IMAGEM DO FL", link.engineMessage, true };
    return { "MOTOR PRONTO", link.engineMessage, false };
}

//==============================================================================
void ReelsCaptureEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::fundo);
    g.addTransform (juce::AffineTransform::scale (uiScale()));

    paintSpine (g);
    paintLoudness (g);
    paintMeters (g);
    paintControls (g);
    paintPreview (g);

    // Rodapé
    g.setColour (colours::linha);
    g.fillRect (mainLeft, footerLineY, mainRight - mainLeft, 1.0f);
    paintFooterStatus (g);
}

void ReelsCaptureEditor::paintSpine (juce::Graphics& g)
{
    g.setColour (colours::vermelho);
    g.fillRect (spineArea);

    if (symbol != nullptr)
        symbol->drawWithin (g, symbolArea, juce::RectanglePlacement::centred, 1.0f);

    if (wordmark != nullptr)
    {
        // Wordmark rodado −90° (lê-se de baixo para cima), centrado em (38, 336)
        const auto bounds = wordmark->getDrawableBounds();
        const float length = wordmarkLength;
        const float thickness = length * bounds.getHeight() / bounds.getWidth();
        const juce::Rectangle<float> area (wordmarkCentre.x - length * 0.5f, wordmarkCentre.y - thickness * 0.5f,
                                           length, thickness);

        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                                         wordmarkCentre.x, wordmarkCentre.y));
        wordmark->drawWithin (g, area, juce::RectanglePlacement::centred, 1.0f);
    }
}

void ReelsCaptureEditor::paintLoudness (juce::Graphics& g)
{
    // Rótulo
    g.setColour (colours::cinzaTexto);
    g.setFont (Fonts::martian (9.0f, 0.08f));
    g.drawText ("LOUDNESS " + dot + " MOMENTARY", juce::Rectangle<float> (mainLeft, 26.0f, 440.0f, 11.0f),
                juce::Justification::centredLeft, false);

    // Número momentary + "LUFS" na mesma linha de base
    const auto bigFont = Fonts::schibsted (Fonts::Schibsted::black, 88.0f, -0.05f);
    const auto number = momentaryLufs <= -99.0f ? minus + juce::String::fromUTF8 ("\xe2\x88\x9e")   // −∞
                                                : signedNumber (momentaryLufs, 1);
    const float baseline = baselineInBox (bigFont, 37.0f, 83.6f);
    drawAtBaseline (g, number, bigFont, mainLeft, baseline, colours::marfim);

    const float numberWidth = Fonts::width (bigFont, number);
    drawAtBaseline (g, "LUFS", Fonts::martian (11.0f), mainLeft + numberWidth + 10.0f, baseline, colours::cinzaTexto);

    // Short-term, à direita
    const auto shortFont = Fonts::schibsted (Fonts::Schibsted::bold, 24.0f, -0.02f);
    const auto shortLabelFont = Fonts::martian (9.0f, 0.04f);
    const auto shortNumber = shortTermLufs <= -99.0f ? minus + juce::String::fromUTF8 ("\xe2\x88\x9e")
                                                     : signedNumber (shortTermLufs, 1);
    const float shortBaseline = baselineInBox (shortFont, 66.6f, 29.0f);
    const float labelWidth = Fonts::width (shortLabelFont, "SHORT");
    const float labelX = mainRight - labelWidth;
    const float numberX = labelX - Fonts::width (shortFont, " ") - Fonts::width (shortFont, shortNumber);
    drawAtBaseline (g, shortNumber, shortFont, numberX, shortBaseline, colours::marfim);
    drawAtBaseline (g, "SHORT", shortLabelFont, labelX, shortBaseline, colours::cinzaTexto);

    // "alvo Reels −14 · 48 kHz"
    const double sr = processor.getCurrentSampleRate();
    const auto rate = std::abs (sr - std::round (sr / 1000.0) * 1000.0) < 1.0 ? juce::String (juce::roundToInt (sr / 1000.0))
                                                                               : juce::String (sr / 1000.0, 1);
    g.setColour (colours::cinzaTexto);
    g.setFont (Fonts::martian (9.0f));
    g.drawText ("alvo Reels " + minus + "14 " + dot + " " + rate + " kHz",
                juce::Rectangle<float> (572.7f, 103.6f, 155.3f, 11.0f), juce::Justification::centredRight, false);
}

void ReelsCaptureEditor::paintMeters (juce::Graphics& g)
{
    const char* names[] = { "L", "R" };

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto bar = meterBar[ch];
        const float rowTop = meterMax[ch].getY();

        g.setColour (colours::cinzaTexto);
        g.setFont (Fonts::martian (9.0f));
        g.drawText (names[ch], juce::Rectangle<float> (mainLeft, rowTop, 12.0f, 11.0f), juce::Justification::centredLeft, false);

        g.setColour (colours::trilhoMedidor);
        g.fillRect (bar);

        const float level = juce::jlimit (0.0f, 1.0f, (peakDb[ch] - meterMinDb) / (meterMaxDb - meterMinDb));
        g.setColour (colours::marfim);
        g.fillRect (bar.withWidth (bar.getWidth() * level));

        // Marca de 0 dB (90% da largura), 5 px para cima e para baixo
        const float zeroX = bar.getX() + bar.getWidth() * (0.0f - meterMinDb) / (meterMaxDb - meterMinDb);
        g.setColour (colours::vermelho);
        g.fillRect (zeroX, bar.getY() - 5.0f, 1.0f, bar.getHeight() + 10.0f);

        // Máximo (clicar limpa)
        const auto maxText = maxDb[ch] <= -99.0f ? juce::String::fromUTF8 ("\xe2\x80\x94") + juce::String::fromUTF8 (" máx")
                                                 : signedNumber (maxDb[ch], 1) + juce::String::fromUTF8 (" máx");
        g.setColour (clipped[ch] ? colours::vermelho : colours::marfim);
        g.setFont (Fonts::martian (9.0f));
        g.drawText (maxText, meterMax[ch], juce::Justification::centredRight, false);
    }
}

void ReelsCaptureEditor::paintControls (juce::Graphics& g)
{
    // Linhas por cima de cada linha de controlo
    g.setColour (colours::linha);
    for (float y : rowLines)
        g.fillRect (mainLeft, y, controlsRight - mainLeft, 1.0f);

    // Rótulos
    g.setColour (colours::cinzaTexto);
    g.setFont (Fonts::martian (9.0f, 0.08f));
    g.drawText (pt ("CÂMARA"), juce::Rectangle<float> (mainLeft, 204.1f, 70.0f, 11.0f), juce::Justification::centredLeft, false);
    g.drawText ("FUNDO",  juce::Rectangle<float> (mainLeft, 245.1f, 70.0f, 11.0f), juce::Justification::centredLeft, false);
    g.drawText ("EFEITO", juce::Rectangle<float> (mainLeft, 286.1f, 70.0f, 11.0f), juce::Justification::centredLeft, false);

    // Câmara: texto e LED
    const bool camera = processor.isCameraEnabled();
    g.setColour (colours::marfim);
    g.setFont (Fonts::schibsted (Fonts::Schibsted::medium, 13.0f));
    g.drawText (camera ? "No topo" : "Desligada", juce::Rectangle<float> (190.0f, 199.6f, 200.0f, 20.0f),
                juce::Justification::centredLeft, false);

    if (camera)
    {
        juce::Path led;
        led.addEllipse (ledArea);
        juce::DropShadow (colours::vermelho, 6, {}).drawForPath (g, led);
        g.setColour (colours::vermelho);
        g.fillPath (led);
    }
    else
    {
        g.setColour (colours::sombraDisco);
        g.fillEllipse (ledArea);
    }

    // Knobs: rótulo e valor
    const juce::String labels[] = { "ZOOM FL", "ALTURA", "SINCRONIA" };
    const float height = (float) heightKnob.getValue();
    const int syncMs = juce::roundToInt (syncKnob.getValue());
    const juce::String values[] = {
        juce::String (zoomKnob.getValue(), 1) + times,
        height > 0.0f ? "+" + juce::String (juce::roundToInt (height)) : signedNumber (height, 0),
        (syncMs > 0 ? "+" + juce::String (syncMs) : signedNumber ((float) syncMs, 0)) + " ms"
    };

    for (int i = 0; i < 3; ++i)
    {
        g.setColour (colours::cinzaTexto);
        g.setFont (Fonts::martian (9.0f, 0.08f));
        g.drawText (labels[i], juce::Rectangle<float> (knobColumnX[i], 378.6f, 84.0f, 11.0f), juce::Justification::centred, false);
        g.setColour (colours::marfim);
        g.setFont (Fonts::martian (10.0f));
        g.drawText (values[i], juce::Rectangle<float> (knobColumnX[i], 393.6f, 84.0f, 12.0f), juce::Justification::centred, false);
    }
}

void ReelsCaptureEditor::paintPreview (juce::Graphics& g)
{
    auto& link = processor.getEngineLink();

    g.setColour (colours::previewFundo);
    g.fillRect (previewArea);

    const auto inner = previewArea.reduced (1.0f);

    if (link.latestPreview.isValid())
    {
        g.drawImage (link.latestPreview, inner, juce::RectanglePlacement::fillDestination);
    }
    else
    {
        auto message = link.engineMessage;
        if (message.isEmpty())
            message = pt ("À espera do motor de vídeo…");

        g.setColour (colours::cinzaTexto);
        g.setFont (Fonts::martian (9.0f));
        g.drawFittedText (message, inner.reduced (12.0f).toNearestInt(), juce::Justification::centred, 14, 1.0f);
    }

    g.setColour (colours::trilhoKnob);
    g.drawRect (previewArea, 1.0f);

    g.setColour (colours::cinzaTexto);
    g.setFont (Fonts::martian (9.0f, 0.08f));
    g.drawText ("ARRASTE " + dot + " RODA = ZOOM", juce::Rectangle<float> (568.0f, 480.6f, 160.0f, 11.0f),
                juce::Justification::centredLeft, false);
}

void ReelsCaptureEditor::paintFooterStatus (juce::Graphics& g)
{
    const bool recording = processor.getSessionState() == ReelsCaptureProcessor::SessionState::recording;

    g.setColour (recording ? colours::vermelho : colours::marfim);
    g.setFont (Fonts::martian (16.0f));
    g.drawText (formatTimecode (recording ? processor.getSecondsRecorded() : 0.0),
                juce::Rectangle<float> (600.0f, 536.0f, 128.0f, 19.0f), juce::Justification::centredRight, false);

    const auto status = currentStatus();
    g.setColour (status.error ? colours::vermelho : colours::cinzaTexto);
    g.setFont (Fonts::martian (8.0f, 0.1f));
    g.drawFittedText (status.text, statusArea, juce::Justification::topRight, 2, 1.0f);
}

void ReelsCaptureEditor::paintOverChildren (juce::Graphics& g)
{
    g.addTransform (juce::AffineTransform::scale (uiScale()));

    // Foco por teclado: contorno de 2 px marfim, afastado 2 px
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr || focused == this || ! isParentOf (focused) || ! focused->isShowing())
        return;

    auto* target = focused;
    while (target != nullptr && target->getParentComponent() != this)
        target = target->getParentComponent();
    if (target != nullptr)
        paintFocusOutline (g, *target);
}

void ReelsCaptureEditor::paintFocusOutline (juce::Graphics& g, juce::Component& target)
{
    auto r = target.getBounds().toFloat();
    float radius = 3.0f;
    const auto styleName = target.getProperties()[style::key].toString();

    if (&target == &backgroundBox || &target == &effectBox)  r = r.withHeight (30.0f);
    if (&target == &cameraToggle)                            { r = r.withHeight (20.0f); radius = 10.0f; }
    if (styleName == style::pillRecord || styleName == style::pillStop) radius = r.getHeight() * 0.5f;
    if (dynamic_cast<juce::Slider*> (&target) != nullptr)    radius = r.getWidth() * 0.5f;

    g.setColour (colours::marfim);
    g.drawRoundedRectangle (r.expanded (3.0f), radius + 3.0f, 2.0f);
}

//==============================================================================
// Rato: pré-visualização (arrastar = mover, roda = zoom) e máximo dos medidores (clicar = limpar)
void ReelsCaptureEditor::mouseDown (const juce::MouseEvent& e)
{
    for (int ch = 0; ch < 2; ++ch)
    {
        if (meterMax[ch].expanded (4.0f).contains (toDesign (e.position)))
        {
            maxDb[0] = maxDb[1] = -100.0f;
            clipped[0] = clipped[1] = false;
            repaint();
            return;
        }
    }

    draggingPreview = previewArea.contains (toDesign (e.position));
    dragStartX = processor.getCropPosition();
    dragStartY = (float) (0.5 - heightKnob.getValue() / 100.0);
    if (draggingPreview)
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ReelsCaptureEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingPreview)
        return;

    const float zoom = (float) zoomKnob.getValue();
    const float dx = (float) e.getDistanceFromDragStartX() / uiScale() / previewArea.getWidth();
    const float dy = (float) e.getDistanceFromDragStartY() / uiScale() / previewArea.getHeight();

    const float newX = juce::jlimit (0.0f, 1.0f, dragStartX - dx / (1.0f + zoom));
    const float newY = juce::jlimit (0.0f, 1.0f, dragStartY - dy * 2.0f / zoom);

    heightKnob.setValue (std::round ((0.5f - newY) * 100.0f), juce::dontSendNotification);
    processor.setFraming (newX, (float) (0.5 - heightKnob.getValue() / 100.0), zoom);
    repaint();
}

void ReelsCaptureEditor::mouseUp (const juce::MouseEvent& e)
{
    draggingPreview = false;
    mouseMove (e);
}

void ReelsCaptureEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto p = toDesign (e.position);
    if (previewArea.contains (p))
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else if (meterMax[0].expanded (4.0f).contains (p) || meterMax[1].expanded (4.0f).contains (p))
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ReelsCaptureEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (previewArea.contains (toDesign (e.position)))
        zoomKnob.setValue (zoomKnob.getValue() * (1.0 + wheel.deltaY * 0.5));
}

void ReelsCaptureEditor::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (previewArea.contains (toDesign (e.position)))
        zoomKnob.setValue (zoomKnob.getValue() * scaleFactor);
}

//==============================================================================
void ReelsCaptureEditor::chooseBackgroundImage()
{
    auto start = processor.getBackgroundImage();
    if (! start.existsAsFile())
        start = juce::File::getSpecialLocation (juce::File::userPicturesDirectory);

    fileChooser = std::make_unique<juce::FileChooser> (pt ("Escolha a imagem de fundo"), start,
                                                       "*.jpg;*.jpeg;*.png;*.heic;*.tif;*.tiff");

    juce::Component::SafePointer<ReelsCaptureEditor> safeThis (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto file = chooser.getResult();
        if (file.existsAsFile())
            safeThis->processor.setBackgroundImage (file);

        safeThis->syncVideoControls();
    });
}

void ReelsCaptureEditor::askToClearFolder()
{
    const auto usage = ReelsCaptureProcessor::measureOutputFolder();

    if (usage.files == 0)
    {
        juce::NativeMessageBox::showAsync (juce::MessageBoxOptions()
                                               .withIconType (juce::MessageBoxIconType::InfoIcon)
                                               .withTitle (pt ("Limpar pasta"))
                                               .withMessage (pt ("A pasta já está vazia."))
                                               .withButton ("OK")
                                               .withAssociatedComponent (this),
                                           nullptr);
        return;
    }

    const auto mb = juce::String ((double) usage.bytes / (1024.0 * 1024.0), 1);
    const auto message = pt ("Vão ser apagados DEFINITIVAMENTE ") + juce::String (usage.files)
                       + pt (" ficheiros (") + mb + pt (" MB): todos os MP4, WAV e vídeos brutos em\n")
                       + pt ("Filmes > C00KUP.\n\nNão vão para o Lixo e não podem ser recuperados.");

    juce::Component::SafePointer<ReelsCaptureEditor> safeThis (this);
    juce::NativeMessageBox::showAsync (juce::MessageBoxOptions()
                                           .withIconType (juce::MessageBoxIconType::WarningIcon)
                                           .withTitle (pt ("Apagar tudo?"))
                                           .withMessage (message)
                                           .withButton (pt ("Apagar tudo"))
                                           .withButton ("Cancelar")
                                           .withAssociatedComponent (this),
                                       [safeThis] (int result)
    {
        if (safeThis != nullptr && result == 0)   // 0 = 1.º botão ("Apagar tudo")
        {
            safeThis->processor.clearOutputFolder();
            safeThis->repaint();
        }
    });
}
