#include "EngineLink.h"

namespace
{
    juce::String pt (const char* utf8) { return juce::String::fromUTF8 (utf8); }
}

EngineLink::EngineLink() : juce::InterprocessConnection (true, magic) {}

EngineLink::~EngineLink()
{
    stopTimer();
    disconnect();
}

juce::File EngineLink::findEngineApp()
{
   #if JUCE_WINDOWS
    // Instalado pelo C00KUP-Setup.exe em C:\Program Files\808College\C00KUP
    auto exe = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                   .getChildFile ("808College").getChildFile ("C00KUP").getChildFile ("C00KUP Engine.exe");
    return exe.existsAsFile() ? exe : juce::File();
   #else
    const juce::String name ("C00KUP Engine.app");

    for (auto dir : { juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Applications"),
                      juce::File ("/Applications") })
    {
        auto app = dir.getChildFile (name);
        if (app.exists())
            return app;
    }

    return {};
   #endif
}

void EngineLink::setActive (bool shouldBeActive)
{
    if (active == shouldBeActive)
        return;

    active = shouldBeActive;

    if (active)
    {
        failedAttempts = 0;
        timerCallback();
        startTimer (1000);
    }
    else
    {
        stopTimer();
        disconnect();
        engineMessage = {};
    }
}

void EngineLink::timerCallback()
{
    if (isConnected())
        return;

    if (connectToSocket ("127.0.0.1", port, 250))
        return;

    ++failedAttempts;

    const auto app = findEngineApp();

    engineMissing = ! app.exists();

    if (! app.exists())
    {
       #if JUCE_WINDOWS
        engineMessage = pt ("Motor não instalado: falta o \"C00KUP Engine\". Volte a correr o C00KUP-Setup.exe.");
       #else
        engineMessage = pt ("Motor não instalado: falta \"C00KUP Engine\" em Aplicações. Volte a correr o instalador.");
       #endif
        return;
    }

    const auto now = juce::Time::getMillisecondCounter();

    if (lastLaunchMs == 0 || now - lastLaunchMs > 15000)
    {
        lastLaunchMs = now;
        engineMessage = pt ("A abrir o motor de vídeo...");
        app.startAsProcess();   // abre como um duplo clique (LaunchServices no Mac, ShellExecute no Windows)
    }
}

bool EngineLink::sendCommand (const juce::var& json)
{
    if (! isConnected())
        return false;

    const auto text = juce::JSON::toString (json, true);
    juce::MemoryBlock block;
    block.append ("J", 1);
    block.append (text.toRawUTF8(), text.getNumBytesAsUTF8());
    return sendMessage (block);
}

void EngineLink::connectionMade()
{
    failedAttempts = 0;
    engineMessage = pt ("Motor ligado.");

    auto* hello = new juce::DynamicObject();
    hello->setProperty ("cmd", "hello");
    hello->setProperty ("client", "plugin");
    sendCommand (juce::var (hello));
}

void EngineLink::connectionLost()
{
    engineCapturing = false;
    engineMessage = pt ("Ligação ao motor perdida.");
    latestPreview = {};
    ++previewCounter;

    juce::var ev (new juce::DynamicObject());
    ev.getDynamicObject()->setProperty ("event", "disconnected");
    if (onEvent) onEvent (ev);
}

void EngineLink::messageReceived (const juce::MemoryBlock& message)
{
    if (message.getSize() < 1)
        return;

    const auto* data = static_cast<const char*> (message.getData());
    const auto type = data[0];

    if (type == 'F')
    {
        auto img = juce::ImageFileFormat::loadFrom (data + 1, message.getSize() - 1);
        if (img.isValid())
        {
            latestPreview = img;
            ++previewCounter;
        }
        return;
    }

    if (type == 'J')
    {
        const auto text = juce::String::fromUTF8 (data + 1, (int) message.getSize() - 1);
        const auto json = juce::JSON::parse (text);

        if (! json.isObject())
            return;

        const auto event = json["event"].toString();

        if (event == "status")
        {
            engineCapturing = (bool) json["capturing"];
            engineMessage = json["message"].toString();
        }

        if (onEvent)
            onEvent (json);
    }
}
