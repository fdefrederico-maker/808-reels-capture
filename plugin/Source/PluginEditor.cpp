#include "PluginEditor.h"

using namespace reels;

namespace
{
    // Texto com acentos: o código-fonte está em UTF-8.
    juce::String pt (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    constexpr int editorWidth  = 760;
    constexpr int editorHeight = 600;

    juce::String formatTime (double seconds)
    {
        const int s = (int) seconds;
        return juce::String (s / 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

ReelsCaptureEditor::ReelsCaptureEditor (ReelsCaptureProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    addAndMakeVisible (scale);
    addAndMakeVisible (meterL);
    addAndMakeVisible (meterR);

    auto setupButton = [this] (juce::TextButton& b, juce::Colour colour)
    {
        b.setColour (juce::TextButton::buttonColourId, colours::panel);
        b.setColour (juce::TextButton::buttonOnColourId, colour);
        b.setColour (juce::TextButton::textColourOffId, colours::text);
        b.setColour (juce::TextButton::textColourOnId, colours::background);
        addAndMakeVisible (b);
    };

    setupButton (recordButton, colours::accent);
    setupButton (stopButton,   colours::textDim);
    setupButton (exportButton, colours::meterGreen);

    recordButton.setButtonText (pt ("\xe2\x97\x8f  Gravar"));    // ●
    stopButton.setButtonText   (pt ("\xe2\x96\xa0  Parar"));     // ■
    exportButton.setButtonText ("Exportar");
    exportButton.setTooltip (pt ("Mostrar o último MP4 no Finder"));

    recordButton.onClick = [this] { processor.startRecording(); updateButtons(); };
    stopButton.onClick   = [this] { processor.stopRecording();  updateButtons(); };
    exportButton.onClick = [this] { processor.revealLastExport(); };

    cropSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    cropSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    cropSlider.setRange (0.0, 1.0, 0.0);
    cropSlider.setValue (processor.getCropPosition(), juce::dontSendNotification);
    cropSlider.setDoubleClickReturnValue (true, 0.5);
    cropSlider.setColour (juce::Slider::trackColourId, colours::panelEdge);
    cropSlider.setColour (juce::Slider::backgroundColourId, colours::panelEdge);
    cropSlider.setColour (juce::Slider::thumbColourId, colours::accent);
    cropSlider.setTooltip (pt ("Que parte da janela do FL aparece no vídeo vertical (duplo clique = centro)"));
    cropSlider.onValueChange = [this] { framingChanged(); };
    addAndMakeVisible (cropSlider);

    for (auto* s : { &zoomSlider, &cropYSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setColour (juce::Slider::trackColourId, colours::panelEdge);
        s->setColour (juce::Slider::backgroundColourId, colours::panelEdge);
        s->setColour (juce::Slider::thumbColourId, colours::accent);
        s->onValueChange = [this] { framingChanged(); };
        addAndMakeVisible (*s);
    }

    zoomSlider.setRange (1.0, 4.0, 0.0);
    zoomSlider.setSkewFactorFromMidPoint (2.0);
    zoomSlider.setValue (processor.getZoom(), juce::dontSendNotification);
    zoomSlider.setDoubleClickReturnValue (true, 1.0);
    zoomSlider.setTooltip (pt ("Zoom da janela do FL: 1x mostra a janela toda na altura (duplo clique = 1x)"));

    cropYSlider.setRange (0.0, 1.0, 0.0);
    cropYSlider.setValue (processor.getCropY(), juce::dontSendNotification);
    cropYSlider.setDoubleClickReturnValue (true, 0.5);
    cropYSlider.setTooltip (pt ("Que parte da janela do FL aparece: cima <-> baixo (só se nota com zoom)"));

    // Câmara e fundo
    cameraToggle.setButtonText (pt ("Câmara no topo"));
    cameraToggle.setColour (juce::ToggleButton::textColourId, colours::text);
    cameraToggle.setColour (juce::ToggleButton::tickColourId, colours::accent);
    cameraToggle.onClick = [this] { processor.setCameraEnabled (cameraToggle.getToggleState()); syncVideoControls(); };
    addAndMakeVisible (cameraToggle);

    using P = ReelsCaptureProcessor;
    backgroundBox.addItem ("Sala original",  P::bgOriginal);
    backgroundBox.addItem ("Sala desfocada", P::bgBlur);
    backgroundBox.addItem ("Imagem...",      P::bgImage);
    backgroundBox.addSeparator();
    backgroundBox.addItem ("Preto",          P::bgBlack);
    backgroundBox.addItem ("Branco",         P::bgWhite);
    backgroundBox.addItem ("Cinzento",       P::bgGrey);
    backgroundBox.addItem ("Verde suave",    P::bgGreen);
    backgroundBox.setColour (juce::ComboBox::backgroundColourId, colours::background);
    backgroundBox.setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
    backgroundBox.setColour (juce::ComboBox::textColourId, colours::text);
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
    addAndMakeVisible (backgroundBox);

    imageButton.setButtonText (pt ("Imagem..."));
    imageButton.setTooltip (pt ("Escolher a imagem de fundo (JPG, PNG, HEIC)"));
    imageButton.setColour (juce::TextButton::buttonColourId, colours::panel);
    imageButton.setColour (juce::TextButton::textColourOffId, colours::text);
    imageButton.onClick = [this] { chooseBackgroundImage(); };
    addAndMakeVisible (imageButton);

    syncVideoControls();

    setSize (editorWidth, editorHeight);

    processor.editorVisibilityChanged (true);
    updateButtons();
    startTimerHz (30);
}

ReelsCaptureEditor::~ReelsCaptureEditor()
{
    stopTimer();
    processor.editorVisibilityChanged (false);
}

void ReelsCaptureEditor::framingChanged()
{
    processor.setFraming ((float) cropSlider.getValue(), (float) cropYSlider.getValue(), (float) zoomSlider.getValue());
    repaint (videoArea);
}

void ReelsCaptureEditor::mouseDown (const juce::MouseEvent& e)
{
    draggingPreview = getPreviewFrame().contains (e.position);
    dragStartX = (float) cropSlider.getValue();
    dragStartY = (float) cropYSlider.getValue();
}

void ReelsCaptureEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingPreview)
        return;

    // Arrastar a imagem: mover para a direita mostra mais do lado esquerdo do FL
    const auto frame = getPreviewFrame();
    const float zoom = (float) zoomSlider.getValue();
    const float dx = (float) e.getDistanceFromDragStartX() / frame.getWidth();
    const float dy = (float) e.getDistanceFromDragStartY() / frame.getHeight();

    cropSlider.setValue (juce::jlimit (0.0, 1.0, (double) (dragStartX - dx / (1.0f + zoom))), juce::dontSendNotification);
    cropYSlider.setValue (juce::jlimit (0.0, 1.0, (double) (dragStartY - dy * 2.0f / zoom)), juce::dontSendNotification);
    framingChanged();
}

void ReelsCaptureEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! getPreviewFrame().contains (e.position))
        return;

    zoomSlider.setValue (zoomSlider.getValue() * (1.0 + wheel.deltaY * 0.5), juce::dontSendNotification);
    framingChanged();
}

void ReelsCaptureEditor::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (! getPreviewFrame().contains (e.position))
        return;

    zoomSlider.setValue (zoomSlider.getValue() * scaleFactor, juce::dontSendNotification);
    framingChanged();
}

void ReelsCaptureEditor::syncVideoControls()
{
    cameraToggle.setToggleState (processor.isCameraEnabled(), juce::dontSendNotification);
    backgroundBox.setSelectedId (processor.getBackgroundId(), juce::dontSendNotification);
    backgroundBox.setEnabled (processor.isCameraEnabled());
    imageButton.setEnabled (processor.isCameraEnabled());
}

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

void ReelsCaptureEditor::updateButtons()
{
    using S = ReelsCaptureProcessor::SessionState;
    const auto state = processor.getSessionState();

    recordButton.setEnabled (state == S::idle);
    stopButton.setEnabled (state == S::recording);
    exportButton.setEnabled (state != S::recording);

    recordButton.setToggleState (state == S::recording, juce::dontSendNotification);

    if (state == S::recording)
        recordButton.setButtonText (pt ("\xe2\x97\x8f  A gravar  ") + formatTime (processor.getSecondsRecorded()));
    else
        recordButton.setButtonText (pt ("\xe2\x97\x8f  Gravar"));

    if (state == S::exporting)
        exportButton.setButtonText ("A exportar " + juce::String (juce::roundToInt (processor.getExportProgress() * 100.0f)) + "%");
    else
        exportButton.setButtonText ("Exportar");
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

    updateButtons();

    auto& link = processor.getEngineLink();
    if (link.previewCounter != lastPreviewCounter)
    {
        lastPreviewCounter = link.previewCounter;
        repaint (previewPanel);
    }

    const auto status = processor.getStatusText() + "|" + link.engineMessage;
    if (status != lastStatus)
    {
        lastStatus = status;
        repaint (statusArea);
        repaint (previewPanel);
    }

    repaint (loudnessPanel);
    repaint (headerArea);
}

juce::Rectangle<float> ReelsCaptureEditor::getPreviewFrame() const
{
    auto inner = previewPanel.reduced (12);
    inner.removeFromBottom (40);   // espaço para a barra de recorte
    const float h = (float) inner.getHeight();
    return juce::Rectangle<float> (h * 9.0f / 16.0f, h).withCentre (inner.toFloat().getCentre());
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
            info << pt ("  \xc2\xb7  WAV convertido para 48 kHz");
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

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (colours::textDim);
    g.drawText ("MASTER", meterPanel.reduced (12, 8).removeFromTop (14), juce::Justification::centredLeft);
    g.drawText ("LOUDNESS", loudnessPanel.reduced (12, 8).removeFromTop (14), juce::Justification::centredLeft);

    // Pico máximo por canal
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

    // Secção VÍDEO
    {
        g.setColour (colours::panelEdge);
        g.fillRect (videoArea.getX(), videoArea.getY() - 10, videoArea.getWidth(), 1);

        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (colours::textDim);
        g.drawText (pt ("VÍDEO"), videoArea.withHeight (14), juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Fundo", backgroundBox.getBounds().withX (videoArea.getX()).withWidth (50),
                    juce::Justification::centredLeft);

        g.drawText ("Zoom FL", zoomSlider.getBounds().withX (videoArea.getX()).withWidth (60),
                    juce::Justification::centredLeft);
        g.drawText (juce::String (zoomSlider.getValue(), 1) + "x",
                    zoomSlider.getBounds().withX (zoomSlider.getRight() + 4).withWidth (40),
                    juce::Justification::centredLeft);
        g.drawText (pt ("Cima/baixo"), cropYSlider.getBounds().withX (videoArea.getX()).withWidth (64),
                    juce::Justification::centredLeft);

        if (processor.getBackgroundId() == ReelsCaptureProcessor::bgImage)
        {
            g.drawFittedText (processor.getBackgroundImage().getFileName(),
                              backgroundBox.getBounds().translated (0, 30).withHeight (16),
                              juce::Justification::centredLeft, 1);
        }
    }

    // Pré-visualização 9:16
    {
        const auto frame = getPreviewFrame();
        auto& link = processor.getEngineLink();

        g.setColour (juce::Colour (0xff0b0c0f));
        g.fillRoundedRectangle (frame, 6.0f);

        if (link.latestPreview.isValid())
        {
            g.drawImage (link.latestPreview, frame, juce::RectanglePlacement::centred);
        }
        else
        {
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (12.0f));
            auto msg = link.engineMessage.isNotEmpty() ? link.engineMessage
                                                       : pt ("À espera do motor de vídeo...");
            g.drawFittedText (pt ("Pré-visualização 9:16\n\n") + msg,
                              frame.reduced (10.0f).toNearestInt(), juce::Justification::centred, 8);
        }

        if (processor.getSessionState() == ReelsCaptureProcessor::SessionState::recording)
        {
            g.setColour (colours::accent);
            g.fillEllipse (frame.getX() + 10.0f, frame.getY() + 10.0f, 10.0f, 10.0f);
        }

        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (frame, 6.0f, 1.0f);

        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (pt ("Arraste = mover  \xc2\xb7  roda = zoom"), cropLabelArea, juce::Justification::centred);
    }

    // Linha de estado
    {
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        auto text = processor.getStatusText();
        if (text.isEmpty())
            text = processor.getEngineLink().engineMessage;
        g.drawFittedText (text, statusArea, juce::Justification::centredLeft, 1);
    }
}

void ReelsCaptureEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    headerArea = area.removeFromTop (32);
    area.removeFromTop (12);

    auto buttons = area.removeFromBottom (44);
    statusArea = area.removeFromBottom (28);
    area.removeFromBottom (4);

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

    // Pré-visualização à direita
    const int previewWidth = (int) std::round ((area.getHeight() - 24 - 40) * 9.0 / 16.0) + 24;
    previewPanel = area.removeFromRight (juce::jmax (previewWidth, 200));
    area.removeFromRight (12);

    {
        auto inner = previewPanel.reduced (12);
        auto bottom = inner.removeFromBottom (40);
        cropLabelArea = bottom.removeFromBottom (14);
        cropSlider.setBounds (bottom.reduced (4, 2));
    }

    meterPanel = area.removeFromLeft (170);
    area.removeFromLeft (12);
    loudnessPanel = area;

    // Secção VÍDEO (dentro do painel do meio, abaixo dos LUFS)
    {
        auto v = loudnessPanel.reduced (12, 8);
        v.removeFromTop (20 + 54 + 6 + 54 + 22);
        videoArea = v.removeFromTop (190);

        auto rows = videoArea;
        rows.removeFromTop (20);
        cameraToggle.setBounds (rows.removeFromTop (26));
        rows.removeFromTop (6);
        auto bgRow = rows.removeFromTop (26);
        bgRow.removeFromLeft (50);
        imageButton.setBounds (bgRow.removeFromRight (84));
        bgRow.removeFromRight (6);
        backgroundBox.setBounds (bgRow);

        rows.removeFromTop (24);   // nome da imagem de fundo
        auto zoomRow = rows.removeFromTop (24);
        zoomRow.removeFromLeft (66);
        zoomRow.removeFromRight (44);
        zoomSlider.setBounds (zoomRow);

        rows.removeFromTop (6);
        auto yRow = rows.removeFromTop (24);
        yRow.removeFromLeft (66);
        yRow.removeFromRight (44);
        cropYSlider.setBounds (yRow);
    }

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
