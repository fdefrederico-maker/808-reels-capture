#include "PluginProcessor.h"
#include "PluginEditor.h"

ReelsCaptureProcessor::ReelsCaptureProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    engineLink.onEvent = [this] (const juce::var& ev) { handleEngineEvent (ev); };
}

bool ReelsCaptureProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // Efeito de passagem: entrada igual à saída.
    return layouts.getMainInputChannelSet() == out;
}

void ReelsCaptureProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);
    blockSeconds.store (samplesPerBlock > 0 ? (double) samplesPerBlock / sampleRate : 0.01);
    numMeterChannels.store (juce::jlimit (1, maxMeterChannels, getTotalNumInputChannels()));
    rmsMeanSquare.fill (0.0);

    for (auto& l : levels)
    {
        l.peak.store (0.0f);
        l.rms.store (0.0f);
    }

    loudness.prepare (sampleRate);
    wavRecorder.prepare (sampleRate);

    // Passa-baixo de 1.ª ordem a ~150 Hz para isolar kick/808
    bassCoeff = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 150.0 / sampleRate));
    bassLowpass = 0.0f;
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

    // Graves para o "Glitch ao ritmo"
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (juce::jmin (1, meterChannels - 1));
        float lp = bassLowpass, peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            lp += bassCoeff * (0.5f * (l[i] + r[i]) - lp);
            peak = juce::jmax (peak, std::abs (lp));
        }
        bassLowpass = lp;
        float prev = bassPeak.load();
        while (peak > prev && ! bassPeak.compare_exchange_weak (prev, peak)) {}
    }

    loudness.process (buffer.getArrayOfReadPointers(), meterChannels, numSamples);
    wavRecorder.pushBlock (buffer.getArrayOfReadPointers(), meterChannels, numSamples);

    // O áudio não é alterado: a entrada passa tal e qual para a saída.
}

//==============================================================================
namespace
{
    juce::String pt (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> props)
    {
        auto* obj = new juce::DynamicObject();
        for (auto& p : props)
            obj->setProperty (p.first, p.second);
        return juce::var (obj);
    }
}

juce::File ReelsCaptureProcessor::getOutputFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMoviesDirectory)
               .getChildFile ("C00KUP");
}

namespace
{
    // Todos os ficheiros (incluindo ocultos e subpastas) dentro da pasta de saída
    juce::Array<juce::File> outputFiles()
    {
        const auto folder = ReelsCaptureProcessor::getOutputFolder();
        if (! folder.isDirectory())
            return {};
        return folder.findChildFiles (juce::File::findFiles, true, "*", juce::File::FollowSymlinks::no);
    }
}

ReelsCaptureProcessor::FolderUsage ReelsCaptureProcessor::measureOutputFolder()
{
    FolderUsage usage;
    for (auto& f : outputFiles())
    {
        ++usage.files;
        usage.bytes += f.getSize();
    }
    return usage;
}

void ReelsCaptureProcessor::clearOutputFolder()
{
    if (! canClearOutputFolder())
    {
        setStatus (pt ("Não dá para limpar a gravar"), true);
        return;
    }

    const auto folder = getOutputFolder();

    // Segurança: só mexe exatamente em ~/Movies/C00KUP (e nunca segue atalhos)
    if (! folder.isDirectory() || folder.isSymbolicLink()
        || folder.getFileName() != "C00KUP"
        || folder.getParentDirectory() != juce::File::getSpecialLocation (juce::File::userMoviesDirectory))
    {
        setStatus (pt ("A pasta já está vazia"));
        return;
    }

    const auto before = measureOutputFolder();
    int failed = 0;

    for (auto& child : folder.findChildFiles (juce::File::findFilesAndDirectories, false, "*"))
    {
        // deleteRecursively apaga de vez (não vai para o Lixo); não segue atalhos
        if (! child.deleteRecursively (false))
            ++failed;
    }

    lastExport = juce::File();
    engineLink.sendCommand (makeObject ({ { "cmd", "forget" } }));

    const auto after = measureOutputFolder();
    const auto freedMB = (double) (before.bytes - after.bytes) / (1024.0 * 1024.0);

    if (failed == 0)
        setStatus (pt ("Pasta limpa · ") + juce::String (before.files - after.files) + pt (" ficheiros · ")
                   + juce::String (freedMB, 0) + " MB");
    else
        setStatus (pt ("Ficaram ") + juce::String (after.files) + pt (" ficheiros por apagar"), true);
}

void ReelsCaptureProcessor::setStatus (const juce::String& shortText, bool isError, const juce::String& detail)
{
    statusText = shortText;
    statusIsError = isError;
    statusDetail = detail.isNotEmpty() ? detail : shortText;
}

void ReelsCaptureProcessor::editorVisibilityChanged (bool editorIsOpen)
{
    editorOpen = editorIsOpen;
    if (! levelTimer.isTimerRunning())
        levelTimer.startTimerHz (30);
    engineLink.setActive (editorOpen || sessionState != SessionState::idle);
}

void ReelsCaptureProcessor::startRecording()
{
    if (sessionState != SessionState::idle)
        return;

    currentTakeName = "C00KUP " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");

    const auto folder = getOutputFolder();
    const auto raw = folder.getChildFile ("Brutos");
    raw.createDirectory();

    juce::String error;
    if (! wavRecorder.start (raw.getChildFile (currentTakeName + ".wav"), error))
    {
        setStatus (pt ("Erro ao criar o WAV"), true, error);
        return;
    }

    sessionState = SessionState::recording;
    engineLink.setActive (true);
    exportProgress = 0.0f;

    videoActive = engineLink.sendCommand (makeObject ({
        { "cmd",    "start" },
        { "video",  raw.getChildFile (currentTakeName + ".mov").getFullPathName() },
        { "output", folder.getChildFile (currentTakeName + ".mp4").getFullPathName() }
    }));

    setStatus (videoActive ? juce::String() : pt ("A gravar só áudio (sem motor)"), ! videoActive);
}

void ReelsCaptureProcessor::stopRecording()
{
    if (sessionState != SessionState::recording)
        return;

    const auto take = wavRecorder.stop();

    if (! take.ok)
    {
        setStatus (pt ("Nada gravado: o FL estava parado?"), true,
                   pt ("O FL não enviou áudio durante a gravação. Carregue em Play no FL antes de gravar."));
        engineLink.sendCommand (makeObject ({ { "cmd", "cancel" } }));
        sessionState = SessionState::idle;
        engineLink.setActive (editorOpen);
        return;
    }

    const bool sent = videoActive && engineLink.sendCommand (makeObject ({
        { "cmd",             "stop" },
        { "wav",             take.file.getFullPathName() },
        { "audioStartTicks", juce::String (heardTicks (take.startTicks)) },   // texto: evita perder precisão
        { "syncOffsetMs",    syncOffsetMs },
        { "audioFrames",     juce::String (take.framesWritten) },
        { "audioSampleRate", WavRecorder::outputSampleRate }
    }));

    if (sent)
    {
        sessionState = SessionState::exporting;
        setStatus ({});
    }
    else
    {
        sessionState = SessionState::idle;
        engineLink.setActive (editorOpen);
        setStatus (pt ("Só áudio guardado (sem vídeo)"), true, take.file.getFullPathName());
    }
}

void ReelsCaptureProcessor::revealLastExport()
{
    if (lastExport.existsAsFile())
    {
        lastExport.revealToUser();          // abre a pasta com o último vídeo selecionado
    }
    else
    {
        const auto folder = getOutputFolder();
        folder.createDirectory();
        folder.startAsProcess();            // abre a pasta no Finder
    }
}

void ReelsCaptureProcessor::setCropPosition (float x)
{
    cropPosition = juce::jlimit (0.0f, 1.0f, x);
    sendCrop();
}

void ReelsCaptureProcessor::setFraming (float x, float y, float zoom)
{
    cropPosition = juce::jlimit (0.0f, 1.0f, x);
    cropY = juce::jlimit (0.0f, 1.0f, y);
    flZoom = juce::jlimit (1.0f, 4.0f, zoom);
    sendCrop();
}

void ReelsCaptureProcessor::sendCrop()
{
    engineLink.sendCommand (makeObject ({ { "cmd", "crop" }, { "x", cropPosition }, { "y", cropY }, { "zoom", flZoom } }));
}

void ReelsCaptureProcessor::setCameraEnabled (bool shouldBeOn)
{
    cameraEnabled = shouldBeOn;
    engineLink.sendCommand (makeObject ({ { "cmd", "camera" }, { "enabled", cameraEnabled } }));
}

void ReelsCaptureProcessor::setBackground (int newId)
{
    if (newId == bgImage && ! backgroundImage.existsAsFile())
        return;   // primeiro é preciso escolher uma imagem

    backgroundId = newId;

    auto colour = [] (double r, double g, double b)
    {
        juce::Array<juce::var> rgb { r, g, b };
        return juce::var (rgb);
    };

    juce::var cmd;
    switch (backgroundId)
    {
        case bgBlur:  cmd = makeObject ({ { "cmd", "background" }, { "mode", "blur" } }); break;
        case bgImage: cmd = makeObject ({ { "cmd", "background" }, { "mode", "image" },
                                          { "path", backgroundImage.getFullPathName() } }); break;
        case bgBlack: cmd = makeObject ({ { "cmd", "background" }, { "mode", "color" }, { "color", colour (0.0, 0.0, 0.0) } }); break;
        case bgWhite: cmd = makeObject ({ { "cmd", "background" }, { "mode", "color" }, { "color", colour (0.95, 0.95, 0.95) } }); break;
        case bgGrey:  cmd = makeObject ({ { "cmd", "background" }, { "mode", "color" }, { "color", colour (0.16, 0.16, 0.18) } }); break;
        case bgGreen: cmd = makeObject ({ { "cmd", "background" }, { "mode", "color" }, { "color", colour (0.55, 0.75, 0.55) } }); break;
        default:      backgroundId = bgOriginal;
                      cmd = makeObject ({ { "cmd", "background" }, { "mode", "original" } }); break;
    }

    engineLink.sendCommand (cmd);
}

void ReelsCaptureProcessor::setBackgroundImage (const juce::File& image)
{
    backgroundImage = image;
    setBackground (bgImage);
}

void ReelsCaptureProcessor::setEffect (int newId)
{
    effectId = juce::jlimit ((int) fxNone, (int) fxTv, newId);
    static const char* names[] = { "none", "vhs", "film", "glitch", "tv" };
    engineLink.sendCommand (makeObject ({ { "cmd", "effect" }, { "name", names[effectId - 1] } }));
}

/** O áudio processado agora só é OUVIDO depois do buffer de saída: é esse o instante
    que conta para a sincronização com a câmara e o ecrã. */
juce::int64 ReelsCaptureProcessor::heardTicks (juce::int64 processedTicks) const
{
    return processedTicks + juce::Time::secondsToHighResolutionTicks (blockSeconds.load());
}

void ReelsCaptureProcessor::sendBassLevel()
{
    const float level = bassPeak.exchange (0.0f);
    if (effectId == fxGlitch && engineLink.isConnected())
    {
        // O pico veio dos blocos dos últimos ~33 ms: carimbar no meio desse intervalo
        const auto ticks = heardTicks (juce::Time::getHighResolutionTicks()
                                       - juce::Time::secondsToHighResolutionTicks (0.016));
        engineLink.sendCommand (makeObject ({ { "cmd", "level" }, { "bass", level },
                                              { "ticks", juce::String (ticks) } }));
    }
}

void ReelsCaptureProcessor::sendVideoSettings()
{
    sendCrop();
    setEffect (effectId);
    setCameraEnabled (cameraEnabled);
    setBackground (backgroundId);
}

void ReelsCaptureProcessor::handleEngineEvent (const juce::var& ev)
{
    const auto event = ev["event"].toString();

    if (event == "welcome")
    {
        sendVideoSettings();
    }
    else if (event == "recording")
    {
        videoActive = true;
    }
    else if (event == "exporting")
    {
        exportProgress = (float) (double) ev["progress"];
    }
    else if (event == "exported")
    {
        lastExport = juce::File (ev["path"].toString());
        setStatus (pt ("MP4 pronto"), false, lastExport.getFullPathName());
        exportProgress = 1.0f;
        if (sessionState == SessionState::exporting)
            sessionState = SessionState::idle;
        engineLink.setActive (editorOpen);
    }
    else if (event == "error")
    {
        const auto message = ev["message"].toString();
        setStatus (message.upToFirstOccurrenceOf (".", false, false).upToFirstOccurrenceOf (":", false, false),
                   true, message);
        if (sessionState == SessionState::recording)
            videoActive = false;
        if (sessionState == SessionState::exporting)
        {
            sessionState = SessionState::idle;
            engineLink.setActive (editorOpen);
        }
    }
    else if (event == "disconnected")
    {
        if (sessionState == SessionState::recording && videoActive)
        {
            videoActive = false;
            setStatus (pt ("O motor fechou: só áudio"), true);
        }
        else if (sessionState == SessionState::exporting)
        {
            sessionState = SessionState::idle;
            setStatus (pt ("O motor fechou a exportar"), true);
            engineLink.setActive (editorOpen);
        }
    }
}

void ReelsCaptureProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("C00KUP");
    xml.setAttribute ("cropX", (double) cropPosition);
    xml.setAttribute ("cropY", (double) cropY);
    xml.setAttribute ("zoom", (double) flZoom);
    xml.setAttribute ("syncMs", (double) syncOffsetMs);
    xml.setAttribute ("uiScale", (double) editorScale);
    xml.setAttribute ("camera", cameraEnabled);
    xml.setAttribute ("effect", effectId);
    xml.setAttribute ("background", backgroundId);
    xml.setAttribute ("backgroundImage", backgroundImage.getFullPathName());
    copyXmlToBinary (xml, dest);
}

void ReelsCaptureProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName ("C00KUP"))
        {
            cropPosition = (float) xml->getDoubleAttribute ("cropX", 0.5);
            cropY = (float) xml->getDoubleAttribute ("cropY", 0.5);
            flZoom = juce::jlimit (1.0f, 4.0f, (float) xml->getDoubleAttribute ("zoom", 1.0));
            setSyncOffsetMs ((float) xml->getDoubleAttribute ("syncMs", 0.0));
            setEditorScale ((float) xml->getDoubleAttribute ("uiScale", 1.0));
            cameraEnabled = xml->getBoolAttribute ("camera", true);
            effectId = juce::jlimit ((int) fxNone, (int) fxTv, xml->getIntAttribute ("effect", fxNone));
            backgroundId = xml->getIntAttribute ("background", bgOriginal);
            const auto path = xml->getStringAttribute ("backgroundImage");
            backgroundImage = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
        }
}

//==============================================================================
juce::AudioProcessorEditor* ReelsCaptureProcessor::createEditor()
{
    return new ReelsCaptureEditor (*this);
}

// Ponto de entrada exigido pelo JUCE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReelsCaptureProcessor();
}
