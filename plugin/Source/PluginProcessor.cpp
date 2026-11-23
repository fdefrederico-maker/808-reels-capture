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
    wavRecorder.prepare (sampleRate);
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
               .getChildFile ("808 Reels Capture");
}

void ReelsCaptureProcessor::editorVisibilityChanged (bool editorIsOpen)
{
    editorOpen = editorIsOpen;
    engineLink.setActive (editorOpen || sessionState != SessionState::idle);
}

void ReelsCaptureProcessor::startRecording()
{
    if (sessionState != SessionState::idle)
        return;

    currentTakeName = "808 Reels " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");

    const auto folder = getOutputFolder();
    const auto raw = folder.getChildFile ("Brutos");
    raw.createDirectory();

    juce::String error;
    if (! wavRecorder.start (raw.getChildFile (currentTakeName + ".wav"), error))
    {
        statusText = error;
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

    statusText = videoActive ? pt ("A gravar áudio e vídeo...")
                             : pt ("A gravar só áudio (motor de vídeo não ligado).");
}

void ReelsCaptureProcessor::stopRecording()
{
    if (sessionState != SessionState::recording)
        return;

    const auto take = wavRecorder.stop();

    if (! take.ok)
    {
        statusText = pt ("Nada gravado: o FL não enviou áudio (a música estava a tocar?).");
        engineLink.sendCommand (makeObject ({ { "cmd", "cancel" } }));
        sessionState = SessionState::idle;
        engineLink.setActive (editorOpen);
        return;
    }

    const bool sent = videoActive && engineLink.sendCommand (makeObject ({
        { "cmd",             "stop" },
        { "wav",             take.file.getFullPathName() },
        { "audioStartTicks", juce::String (take.startTicks) },   // texto: evita perder precisão
        { "audioFrames",     juce::String (take.framesWritten) },
        { "audioSampleRate", WavRecorder::outputSampleRate }
    }));

    if (sent)
    {
        sessionState = SessionState::exporting;
        statusText = pt ("A exportar o MP4...");
    }
    else
    {
        sessionState = SessionState::idle;
        engineLink.setActive (editorOpen);
        statusText = pt ("Áudio guardado (sem vídeo): ") + take.file.getFileName();
    }
}

void ReelsCaptureProcessor::revealLastExport()
{
    if (lastExport.existsAsFile())
        lastExport.revealToUser();
    else
        getOutputFolder().revealToUser();
}

void ReelsCaptureProcessor::setCropPosition (float x)
{
    cropPosition = juce::jlimit (0.0f, 1.0f, x);
    sendCrop();
}

void ReelsCaptureProcessor::sendCrop()
{
    engineLink.sendCommand (makeObject ({ { "cmd", "crop" }, { "x", cropPosition } }));
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

void ReelsCaptureProcessor::sendVideoSettings()
{
    sendCrop();
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
        statusText = pt ("MP4 pronto: ") + lastExport.getFileName();
        exportProgress = 1.0f;
        if (sessionState == SessionState::exporting)
            sessionState = SessionState::idle;
        engineLink.setActive (editorOpen);
    }
    else if (event == "error")
    {
        statusText = ev["message"].toString();
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
            statusText = pt ("O motor fechou: a gravar só áudio.");
        }
        else if (sessionState == SessionState::exporting)
        {
            sessionState = SessionState::idle;
            statusText = pt ("O motor fechou durante a exportação.");
            engineLink.setActive (editorOpen);
        }
    }
}

void ReelsCaptureProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("ReelsCapture");
    xml.setAttribute ("cropX", (double) cropPosition);
    xml.setAttribute ("camera", cameraEnabled);
    xml.setAttribute ("background", backgroundId);
    xml.setAttribute ("backgroundImage", backgroundImage.getFullPathName());
    copyXmlToBinary (xml, dest);
}

void ReelsCaptureProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName ("ReelsCapture"))
        {
            cropPosition = (float) xml->getDoubleAttribute ("cropX", 0.5);
            cameraEnabled = xml->getBoolAttribute ("camera", true);
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
