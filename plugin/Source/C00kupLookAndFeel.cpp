#include "C00kupLookAndFeel.h"
#include "BinaryData.h"

namespace c00kup
{
//==============================================================================
// Fontes
namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }

    juce::Typeface::Ptr typeface (Fonts::Schibsted w)
    {
        static auto medium = load (BinaryData::SchibstedGroteskMedium_ttf, BinaryData::SchibstedGroteskMedium_ttfSize);
        static auto bold   = load (BinaryData::SchibstedGroteskBold_ttf,   BinaryData::SchibstedGroteskBold_ttfSize);
        static auto black  = load (BinaryData::SchibstedGroteskBlack_ttf,  BinaryData::SchibstedGroteskBlack_ttfSize);

        switch (w)
        {
            case Fonts::Schibsted::medium: return medium;
            case Fonts::Schibsted::bold:   return bold;
            case Fonts::Schibsted::black:  return black;
        }
        return medium;
    }

    juce::Typeface::Ptr martianTypeface()
    {
        static auto regular = load (BinaryData::MartianMonoRegular_ttf, BinaryData::MartianMonoRegular_ttfSize);
        return regular;
    }

    /** Fonte com tamanho em px (como o font-size do CSS) e letter-spacing em em. */
    juce::Font makeFont (juce::Typeface::Ptr tf, float px, float letterSpacingEm)
    {
        juce::Font font (juce::FontOptions (tf).withPointHeight (px));
        if (std::abs (letterSpacingEm) > 1.0e-6f && font.getHeight() > 0.0f)
            // O JUCE mede o espaçamento em proporção da altura da fonte; o CSS, em proporção do tamanho (em)
            font = font.withExtraKerningFactor (letterSpacingEm * px / font.getHeight());
        return font;
    }

    const juce::Colour withEnabled (juce::Colour c, const juce::Component& comp)
    {
        return comp.isEnabled() ? c : c.withMultipliedAlpha (0.4f);
    }
}

juce::Font Fonts::schibsted (Schibsted weight, float px, float letterSpacingEm)
{
    return makeFont (typeface (weight), px, letterSpacingEm);
}

juce::Font Fonts::martian (float px, float letterSpacingEm)
{
    return makeFont (martianTypeface(), px, letterSpacingEm);
}

float Fonts::width (const juce::Font& font, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (font, text);
}

void drawAtBaseline (juce::Graphics& g, const juce::String& text, const juce::Font& font,
                     float x, float baseline, juce::Colour colour)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, x, baseline);
    g.setColour (colour);
    glyphs.draw (g);
}

float baselineInBox (const juce::Font& font, float boxTop, float boxHeight)
{
    return boxTop + (boxHeight - font.getHeight()) * 0.5f + font.getAscent();
}

namespace
{
    std::unique_ptr<juce::Drawable> svg (const char* data, int size)
    {
        if (auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data, size)))
            return juce::Drawable::createFromSVG (*xml);
        return {};
    }
}

std::unique_ptr<juce::Drawable> createSymbol()
{
    return svg (BinaryData::simbolo808preto_svg, BinaryData::simbolo808preto_svgSize);
}

std::unique_ptr<juce::Drawable> createWordmark()
{
    return svg (BinaryData::c00kupwordmark_svg, BinaryData::c00kupwordmark_svgSize);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::fundo);
    setColour (juce::PopupMenu::backgroundColourId, colours::fundo);
    setColour (juce::PopupMenu::textColourId, colours::marfim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::linha);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::marfim);
    setColour (juce::ComboBox::textColourId, colours::marfim);
    setColour (juce::TooltipWindow::backgroundColourId, colours::superficie);
    setColour (juce::TooltipWindow::textColourId, colours::marfim);
    setColour (juce::TooltipWindow::outlineColourId, colours::bordaBotao);
}

//==============================================================================
// Knob: arco de raio 19 / traço 3, 270° de curso; corpo de raio 13; ponteiro até ao raio 10.
void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const float cx = (float) x + (float) width * 0.5f;
    const float cy = (float) y + (float) height * 0.5f;
    const float scale = (float) juce::jmin (width, height) / 48.0f;   // desenhado para 48×48
    const float arcRadius = 19.0f * scale;
    const juce::PathStrokeType stroke (3.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    const float valueAngle = startAngle + sliderPos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (cx, cy, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (withEnabled (colours::trilhoKnob, slider));
    g.strokePath (track, stroke);

    float fromAngle = startAngle;
    if ((bool) slider.getProperties()[style::bipolar])
    {
        const auto zeroPos = (float) slider.valueToProportionOfLength (0.0);
        fromAngle = startAngle + zeroPos * (endAngle - startAngle);
    }

    if (std::abs (valueAngle - fromAngle) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (cx, cy, arcRadius, arcRadius, 0.0f,
                           juce::jmin (fromAngle, valueAngle), juce::jmax (fromAngle, valueAngle), true);
        g.setColour (withEnabled (colours::vermelho, slider));
        g.strokePath (arc, stroke);
    }

    const float bodyRadius = 13.0f * scale;
    g.setColour (withEnabled (colours::knob, slider));
    g.fillEllipse (cx - bodyRadius, cy - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);
    g.setColour (withEnabled (colours::bordaKnob, slider));
    g.drawEllipse (cx - bodyRadius + 0.5f, cy - bodyRadius + 0.5f, bodyRadius * 2.0f - 1.0f, bodyRadius * 2.0f - 1.0f, 1.0f);

    const float pointerLength = 10.0f * scale;
    const juce::Point<float> tip (cx + pointerLength * std::sin (valueAngle), cy - pointerLength * std::cos (valueAngle));
    juce::Path pointer;
    pointer.startNewSubPath (cx, cy);
    pointer.lineTo (tip);
    g.setColour (withEnabled (colours::marfim, slider));
    g.strokePath (pointer, juce::PathStrokeType (2.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
// Botões
namespace
{
    juce::String styleOf (const juce::Component& c) { return c.getProperties()[style::key].toString(); }

    juce::Font squareFont()  { return Fonts::schibsted (Fonts::Schibsted::bold, 10.0f, 0.18f); }
    juce::Font pillFont()    { return Fonts::schibsted (Fonts::Schibsted::bold, 13.0f, 0.22f); }
    juce::Font linkFont()    { return Fonts::schibsted (Fonts::Schibsted::bold, 11.0f, 0.2f); }
}

float LookAndFeel::pillWidth (const juce::String& s, const juce::String& text)
{
    const float textWidth = Fonts::width (pillFont(), text);
    if (s == style::pillRecord)
        return 1.0f + 10.0f + 32.0f + 12.0f + textWidth + 22.0f + 1.0f;   // borda, padding, disco, espaço, texto, padding, borda
    return 1.0f + 20.0f + 12.0f + 10.0f + textWidth + 20.0f + 1.0f;      // quadrado de 12 e 10 de espaço
}

float LookAndFeel::linkWidth (const juce::String& text)
{
    return 8.0f + Fonts::width (linkFont(), text) + 8.0f;
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                        bool isMouseOver, bool)
{
    const auto s = styleOf (button);
    const auto bounds = button.getLocalBounds().toFloat();
    const bool hover = isMouseOver && button.isEnabled();

    if (s == style::link)
        return;   // só texto

    if (s == style::square)
    {
        const auto r = bounds.reduced (0.5f);
        g.setColour (withEnabled (colours::superficie, button));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (withEnabled (hover ? colours::bordaHover : colours::bordaBotao, button));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        return;
    }

    if (s == style::pillRecord || s == style::pillStop)
    {
        const bool stopIdle = (s == style::pillStop) && ! button.isEnabled();
        const auto r = bounds.reduced (0.5f);
        const float radius = r.getHeight() * 0.5f;

        g.setColour (stopIdle ? colours::pararFundo : withEnabled (colours::superficie, button));
        g.fillRoundedRectangle (r, radius);
        g.setColour (stopIdle ? colours::pararBorda
                              : withEnabled (hover ? colours::bordaHover : colours::bordaBotao, button));
        g.drawRoundedRectangle (r, radius, 1.0f);
    }
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool isMouseOver, bool)
{
    const auto s = styleOf (button);
    const auto bounds = button.getLocalBounds().toFloat();
    const auto text = button.getButtonText();

    if (s == style::square)
    {
        const auto font = squareFont();
        g.setFont (font);
        g.setColour (withEnabled (colours::cinzaBotao, button));
        // o espaçamento do CSS também fica depois da última letra: compensar para centrar
        g.drawText (text, bounds.withTrimmedLeft (font.getExtraKerningFactor() * font.getHeight()),
                    juce::Justification::centred, false);
        return;
    }

    if (s == style::link)
    {
        const auto font = linkFont();
        const bool hover = isMouseOver && button.isEnabled();
        const auto colour = withEnabled (hover ? colours::marfim : colours::cinzaBotao, button);
        const float x = 8.0f;
        const float baseline = baselineInBox (font, 0.0f, bounds.getHeight());
        drawAtBaseline (g, text, font, x, baseline, colour);
        const float textWidth = Fonts::width (font, text);
        g.setColour (colour);
        g.fillRect (x, baseline + 4.0f, textWidth, 1.0f);   // sublinhado a 4 px
        return;
    }

    if (s == style::pillRecord)
    {
        const float cy = bounds.getCentreY();
        const juce::Rectangle<float> disc (11.0f, cy - 16.0f, 32.0f, 32.0f);

        if ((bool) button.getProperties()[style::recording])
        {
            const float pulse = (float) (double) button.getProperties()[style::pulse];   // 0..1, a 1 Hz
            // Brilho de 18 px a 60% (a pulsar) e anel de 6 px a 25%
            juce::Path glowShape;
            glowShape.addEllipse (disc);
            juce::DropShadow (colours::vermelho.withAlpha (0.6f * (0.45f + 0.55f * pulse)), 18, {}).drawForPath (g, glowShape);
            g.setColour (colours::vermelho.withAlpha (0.25f));
            g.fillEllipse (disc.expanded (6.0f));
        }

        g.setColour (withEnabled (colours::sombraDisco, button));
        g.fillEllipse (disc);
        g.setColour (withEnabled (colours::vermelho, button));
        g.fillEllipse (disc.reduced (3.0f));

        const auto font = pillFont();
        drawAtBaseline (g, text, font, disc.getRight() + 12.0f, baselineInBox (font, 0.0f, bounds.getHeight()),
                        withEnabled (colours::marfim, button));
        return;
    }

    if (s == style::pillStop)
    {
        const bool active = button.isEnabled();
        const float cy = bounds.getCentreY();
        const juce::Rectangle<float> square (21.0f, cy - 6.0f, 12.0f, 12.0f);
        g.setColour (active ? colours::marfim : colours::pararIcone);
        g.fillRect (square);

        const auto font = pillFont();
        drawAtBaseline (g, text, font, square.getRight() + 10.0f, baselineInBox (font, 0.0f, bounds.getHeight()),
                        active ? colours::marfim : colours::pararTexto);
        return;
    }

    LookAndFeel_V4::drawButtonText (g, button, isMouseOver, false);
}

//==============================================================================
// Interruptor 38×20 (raio 10) com bolinha marfim de 14 px
void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    if (styleOf (button) != style::toggleSwitch)
    {
        LookAndFeel_V4::drawToggleButton (g, button, false, false);
        return;
    }

    const juce::Rectangle<float> r (0.0f, 0.0f, 38.0f, 20.0f);
    g.setColour (colours::linha);                       // realce inferior de 1 px
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 10.0f);
    g.setColour (withEnabled (colours::campo, button));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), 9.5f, 1.0f);

    const float knobX = button.getToggleState() ? 38.0f - 1.0f - 2.0f - 14.0f : 3.0f;
    g.setColour (withEnabled (colours::marfim, button));
    g.fillEllipse (knobX, 3.0f, 14.0f, 14.0f);
}

//==============================================================================
// Campo rebaixado: 30 px de altura (+1 px de realce por baixo)
void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int, bool, int, int, int, int, juce::ComboBox& box)
{
    const juce::Rectangle<float> r (0.0f, 0.0f, (float) width, 30.0f);

    g.setColour (colours::linha);                       // realce de 1 px em baixo
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 3.0f);
    g.setColour (withEnabled (colours::campo, box));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    g.fillRect (r.getX() + 2.0f, r.getY() + 1.0f, r.getWidth() - 4.0f, 1.0f);   // sombra interior em cima

    // Chevron 10×6, traço 1,5
    const float chevronX = (float) width - 11.0f - 10.0f;
    const float chevronY = 15.0f - 3.0f;
    juce::Path chevron;
    chevron.startNewSubPath (chevronX + 1.0f, chevronY + 1.0f);
    chevron.lineTo (chevronX + 5.0f, chevronY + 5.0f);
    chevron.lineTo (chevronX + 9.0f, chevronY + 1.0f);
    g.setColour (withEnabled (colours::marfim, box));
    g.strokePath (chevron, juce::PathStrokeType (1.5f));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return Fonts::schibsted (Fonts::Schibsted::medium, 13.0f);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (11, 0, box.getWidth() - 11 - 30, 30);
    label.setBorderSize ({});
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, colours::marfim);
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::PopupMenu::Options LookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
{
    return LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label)
               .withMinimumWidth (box.getWidth())
               .withStandardItemHeight (28);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (colours::fundo);
    g.setColour (colours::bordaBotao);
    g.drawRect (0, 0, width, height, 1);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                     bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool,
                                     const juce::String& text, const juce::String&,
                                     const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (colours::linha);
        g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (colours::linha);
        g.fillRect (area.reduced (1, 0));
    }

    if (isTicked)
    {
        g.setColour (colours::vermelho);
        g.fillEllipse ((float) area.getX() + 10.0f, (float) area.getCentreY() - 3.5f, 7.0f, 7.0f);
    }

    g.setFont (getPopupMenuFont());
    g.setColour (isActive ? colours::marfim : colours::cinzaTexto);
    g.drawText (text, area.withTrimmedLeft (26).withTrimmedRight (10), juce::Justification::centredLeft, true);
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return Fonts::schibsted (Fonts::Schibsted::medium, 13.0f);
}

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int,
                                             int& idealWidth, int& idealHeight)
{
    idealHeight = isSeparator ? 9 : 28;
    idealWidth = (int) Fonts::width (getPopupMenuFont(), text) + 46;
}

//==============================================================================
void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    g.fillAll (colours::superficie);
    g.setColour (colours::bordaBotao);
    g.drawRect (0, 0, width, height, 1);
    g.setColour (colours::marfim);
    g.setFont (Fonts::martian (10.0f));
    g.drawFittedText (text, juce::Rectangle<int> (width, height).reduced (8, 6), juce::Justification::centredLeft, 6);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                    juce::Rectangle<int> parentArea)
{
    const int maxWidth = 320;
    const auto font = Fonts::martian (10.0f);
    juce::AttributedString s;
    s.append (tipText, font);
    juce::TextLayout layout;
    layout.createLayout (s, (float) maxWidth - 16.0f);
    const int w = juce::jmin (maxWidth, (int) layout.getWidth() + 18);
    const int h = (int) layout.getHeight() + 14;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h).constrainedWithin (parentArea);
}
} // namespace c00kup
