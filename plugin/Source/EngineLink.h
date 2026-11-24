#pragma once

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <functional>

// Ligação ao motor "C00KUP Engine" (app em Swift) por socket local (127.0.0.1).
//
// Formato de cada mensagem (igual ao InterprocessConnection do JUCE):
//   [4 bytes número mágico LE][4 bytes tamanho LE][conteúdo]
// O 1.º byte do conteúdo indica o tipo:
//   'J' = texto JSON (comandos e estados)
//   'F' = imagem JPEG da pré-visualização 9:16
class EngineLink : public juce::InterprocessConnection,
                   private juce::Timer
{
public:
    static constexpr int port = 48809;   // 48808 era o "808 Reels Engine" antigo
    static constexpr juce::uint32 magic = 0x4B303043;   // "C00K"

    EngineLink();
    ~EngineLink() override;

    // Quando ativo, tenta manter a ligação (e abre o motor se não estiver a correr).
    void setActive (bool shouldBeActive);

    bool sendCommand (const juce::var& json);

    // Estado mais recente (só usar na thread de mensagens)
    juce::Image latestPreview;
    int previewCounter = 0;
    juce::String engineMessage;        // texto curto para mostrar ao utilizador
    bool engineCapturing = false;      // o motor está a captar a janela do FL
    bool engineMissing = false;        // a app do motor não está instalada

    std::function<void (const juce::var&)> onEvent;   // eventos JSON do motor

    static juce::File findEngineApp();

private:
    void connectionMade() override;
    void connectionLost() override;
    void messageReceived (const juce::MemoryBlock& message) override;
    void timerCallback() override;

    bool active = false;
    juce::uint32 lastLaunchMs = 0;
    int failedAttempts = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineLink)
};
