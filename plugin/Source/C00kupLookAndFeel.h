#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Identidade visual do C00KUP (808College) — ver design-c00kup/SPEC.md.
namespace c00kup
{
//==============================================================================
namespace colours
{
    const juce::Colour fundo        { 0xff0a0a0a };
    const juce::Colour vermelho     { 0xffc61621 };
    const juce::Colour marfim       { 0xfff1ece4 };
    const juce::Colour cinzaTexto   { 0xff8c877f };
    const juce::Colour cinzaBotao   { 0xffc9c3b6 };
    const juce::Colour superficie   { 0xff161616 };
    const juce::Colour bordaBotao   { 0xff2c2c2c };
    const juce::Colour bordaHover   { 0xff3a3a3a };
    const juce::Colour campo        { 0xff050505 };
    const juce::Colour linha        { 0xff1f1f1f };
    const juce::Colour trilhoMedidor{ 0xff1d1d1d };
    const juce::Colour trilhoKnob   { 0xff262626 };
    const juce::Colour knob         { 0xff1a1a1a };
    const juce::Colour bordaKnob    { 0xff2f2f2f };
    const juce::Colour sombraDisco  { 0xff2a1214 };
    const juce::Colour previewFundo { 0xff121316 };

    // Pílula PARAR quando inativa
    const juce::Colour pararFundo   { 0xff111111 };
    const juce::Colour pararBorda   { 0xff222222 };
    const juce::Colour pararTexto   { 0xff6e6a62 };
    const juce::Colour pararIcone   { 0xff4a4842 };
}

//==============================================================================
// Fontes embutidas (BinaryData). Tamanhos em px como no CSS (font-size) e
// espaçamento entre letras em "em" (letter-spacing do CSS).
struct Fonts
{
    enum class Schibsted { medium, bold, black };

    static juce::Font schibsted (Schibsted weight, float px, float letterSpacingEm = 0.0f);
    static juce::Font martian (float px, float letterSpacingEm = 0.0f);

    /** Largura do texto com esta fonte (inclui o espaçamento entre letras). */
    static float width (const juce::Font& font, const juce::String& text);
};

/** Desenha texto com a linha de base em `baseline` (para alinhar textos de tamanhos diferentes). */
void drawAtBaseline (juce::Graphics& g, const juce::String& text, const juce::Font& font,
                     float x, float baseline, juce::Colour colour);

/** Linha de base do texto centrado verticalmente numa caixa (como o CSS faz). */
float baselineInBox (const juce::Font& font, float boxTop, float boxHeight);

/** Símbolo 808College e wordmark C00KUP (SVG embutidos). */
std::unique_ptr<juce::Drawable> createSymbol();
std::unique_ptr<juce::Drawable> createWordmark();

//==============================================================================
// Estilos de botão (propriedade "style" do componente)
namespace style
{
    const juce::Identifier key       { "style" };
    const juce::String square        { "square" };    // LIMPAR PASTA, IMAGEM
    const juce::String pillRecord    { "pillRecord" };
    const juce::String pillStop      { "pillStop" };
    const juce::String link          { "link" };      // ABRIR PASTA
    const juce::String toggleSwitch  { "switch" };

    const juce::Identifier bipolar   { "bipolar" };   // knobs com zero ao centro
    const juce::Identifier recording { "recording" }; // pílula GRAVAR a gravar
    const juce::Identifier pulse     { "pulse" };     // 0..1, brilho do disco a gravar
}

//==============================================================================
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    // Knobs
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;

    // Botões (quadrados, pílulas, link)
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;

    // Interruptor
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isMouseOverButton, bool isButtonDown) override;

    // Campos rebaixados (ComboBox) e menus
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;

    // Dicas (tooltips)
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    /** Largura ideal de uma pílula / link para o texto dado. */
    static float pillWidth (const juce::String& style, const juce::String& text);
    static float linkWidth (const juce::String& text);
};
} // namespace c00kup
