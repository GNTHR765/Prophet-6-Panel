#include "MoogLook.h"

using namespace juce;

const Colour MoogLook::panel      { 0xff1a1a1a };
const Colour MoogLook::panelLight { 0xff242424 };
const Colour MoogLook::rule       { 0xff5a5a5a };
const Colour MoogLook::ink        { 0xffe8e4d8 };
const Colour MoogLook::inkDim     { 0xff9a9689 };
const Colour MoogLook::cream      { 0xffe9e2cf };
const Colour MoogLook::creamDark  { 0xffb8b09b };
const Colour MoogLook::chrome     { 0xffc8c8c8 };
const Colour MoogLook::accent     { 0xffd9a441 };
const Colour MoogLook::modColour[7] = { Colour (0xffd9a441), Colour (0xff5fb8d8), Colour (0xffb48ce6), Colour (0xff7fc96b), Colour (0xffe07a6a), Colour (0xff5fd8b0), Colour (0xffd4d45a) };   // LFO 1 amber, LFO 2 blue, LFO 3 violet, envelope 1 green, shape 1 coral, shape 2 teal, envelope 2 yellow

Font MoogLook::silk (float size, bool bold)
{
    // legibility tuning for light-on-dark (DIN 1450 / MIL-STD-1472): ~20 % larger than the original panel, 2 % wider tracking
    return Font (FontOptions (Font::getDefaultSansSerifFontName(), size * 1.2f, bold ? Font::bold : Font::plain)).withExtraKerningFactor (0.02f);
}

MoogLook::MoogLook()
{
    setColour (ResizableWindow::backgroundColourId, panel);
    setColour (Label::textColourId, ink);
    setColour (Slider::textBoxTextColourId, ink);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (ComboBox::backgroundColourId, panelLight);
    setColour (ComboBox::textColourId, ink);
    setColour (ComboBox::outlineColourId, rule);
    setColour (ComboBox::arrowColourId, inkDim);
    setColour (PopupMenu::backgroundColourId, panelLight);
    setColour (PopupMenu::textColourId, ink);
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff3a3a3a));
    setColour (PopupMenu::highlightedTextColourId, cream);
    setColour (TextButton::buttonColourId, panelLight);
    setColour (TextButton::textColourOffId, ink);
    setColour (TextButton::textColourOnId, panel);
    setColour (TextButton::buttonOnColourId, accent);
}

void MoogLook::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                 float startAngle, float endAngle, Slider&)
{
    const float size = (float) jmin (w, h);
    const Rectangle<float> r ((float) x + ((float) w - size) * 0.5f, (float) y + ((float) h - size) * 0.5f, size, size);
    const auto c = r.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    // tick marks around the skirt
    g.setColour (inkDim);
    const int ticks = 11;
    for (int i = 0; i < ticks; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / (float) (ticks - 1);
        const float r1 = size * 0.5f, r0 = size * 0.44f;
        Point<float> p0 (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a));
        Point<float> p1 (c.x + r1 * std::sin (a), c.y - r1 * std::cos (a));
        g.drawLine (Line<float> (p0, p1), i == 0 || i == ticks - 1 || i == 5 ? 1.4f : 0.8f);
    }

    // black skirt
    const auto skirt = r.reduced (size * 0.09f);
    g.setColour (Colour (0xff0c0c0c));
    g.fillEllipse (skirt);
    g.setColour (Colour (0xff3c3c3c));
    g.drawEllipse (skirt, 1.0f);

    // cream cap with a soft rim
    const auto cap = r.reduced (size * 0.22f);
    g.setGradientFill (ColourGradient (cream, cap.getX(), cap.getY(), creamDark, cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (Colour (0xff6b6555));
    g.drawEllipse (cap, 1.0f);

    // pointer
    const float pr = cap.getWidth() * 0.5f;
    Point<float> tip (c.x + pr * 0.92f * std::sin (angle), c.y - pr * 0.92f * std::cos (angle));
    Point<float> base (c.x + pr * 0.25f * std::sin (angle), c.y - pr * 0.25f * std::cos (angle));
    g.setColour (Colour (0xff1a1a1a));
    g.drawLine (Line<float> (base, tip), jmax (2.f, size * 0.05f));
}

void MoogLook::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                 Slider::SliderStyle style, Slider&)
{
    if (style == Slider::LinearVertical)
    {
        const float cx = (float) x + (float) w * 0.5f;
        g.setColour (Colour (0xff0c0c0c));
        g.fillRoundedRectangle (cx - 3.f, (float) y, 6.f, (float) h, 2.f);
        g.setColour (rule);
        for (int i = 0; i <= 10; ++i)
        {
            const float ty = (float) y + (float) h * (float) i / 10.f;
            g.drawLine (cx + 6.f, ty, cx + (i % 5 == 0 ? 12.f : 9.f), ty, 0.8f);
        }
        // fader cap
        const float capH = 16.f, capW = (float) jmin (w, 26);
        Rectangle<float> cap (cx - capW * 0.5f, pos - capH * 0.5f, capW, capH);
        g.setGradientFill (ColourGradient (cream, cap.getX(), cap.getY(), creamDark, cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 2.f);
        g.setColour (Colour (0xff1a1a1a));
        g.drawLine (cap.getX() + 3.f, cap.getCentreY(), cap.getRight() - 3.f, cap.getCentreY(), 1.5f);
    }
    else
    {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour (Colour (0xff0c0c0c));
        g.fillRoundedRectangle ((float) x, cy - 3.f, (float) w, 6.f, 2.f);
        Rectangle<float> cap (pos - 8.f, cy - 10.f, 16.f, 20.f);
        g.setColour (cream); g.fillRoundedRectangle (cap, 2.f);
    }
}

void MoogLook::drawToggleButton (Graphics& g, ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    // small toggle switch. ON is unmistakable: the switch is lit in the accent colour with its lever up and the label bright;
    // OFF is a dark switch with the lever down and a dim label
    Rectangle<float> sw (r.getX() + 1.f, r.getCentreY() - 8.f, 16.f, 16.f);
    g.setColour (on ? accent : Colour (0xff0c0c0c)); g.fillEllipse (sw);
    g.setColour (on ? accent.brighter (0.3f) : rule); g.drawEllipse (sw, 1.f);
    const float ly = on ? sw.getY() + 3.f : sw.getBottom() - 3.f;
    g.setColour (on ? Colour (0xff141414) : chrome.withAlpha (0.55f));
    g.drawLine (sw.getCentreX(), sw.getCentreY(), sw.getCentreX(), ly, 2.5f);
    g.fillEllipse (sw.getCentreX() - 2.f, ly - 2.f, 4.f, 4.f);
    g.setColour (on ? cream : ink.withAlpha (0.5f));
    g.setFont (silk (10.5f));
    g.drawText (b.getButtonText().toUpperCase(), r.withTrimmedLeft (23.f).toNearestInt(), Justification::centredLeft, false);
}

void MoogLook::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    Rectangle<float> r (0.f, 0.f, (float) w, (float) h);
    g.setColour (Colour (0xff0c0c0c)); g.fillRoundedRectangle (r, 3.f);
    g.setColour (box.hasKeyboardFocus (true) ? cream : rule); g.drawRoundedRectangle (r.reduced (0.5f), 3.f, 1.f);
    Path p; const float ax = (float) w - 11.f, ay = (float) h * 0.5f;
    p.addTriangle (ax - 4.f, ay - 2.f, ax + 4.f, ay - 2.f, ax, ay + 3.f);
    g.setColour (inkDim); g.fillPath (p);
}

void MoogLook::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (4, 0, box.getWidth() - 20, box.getHeight());
    label.setFont (silk (11.5f));
    label.setJustificationType (Justification::centredLeft);
}

void MoogLook::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent : (down || over ? Colour (0xff2e2e2e) : panelLight));
    g.fillRoundedRectangle (r, 3.f);
    g.setColour (on ? accent.darker (0.4f) : rule);
    g.drawRoundedRectangle (r, 3.f, 1.f);
}

Font MoogLook::getLabelFont (Label&) { return silk (11.f); }
Font MoogLook::getComboBoxFont (ComboBox&) { return silk (11.5f); }
Font MoogLook::getPopupMenuFont() { return silk (13.f); }
void MoogLook::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (panelLight); g.setColour (rule); g.drawRect (0, 0, w, h);
}

Label* MoogLook::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (silk (11.f));
    l->setColour (Label::textColourId, cream);            // the value is what you read most: full off-white, not dimmed
    l->setJustificationType (Justification::centred);
    return l;
}

void MoogLook::drawCornerResizer (Graphics& g, int w, int h, bool over, bool)
{
    g.setColour (over ? cream : creamDark);
    const float lw = 2.f;
    for (int i = 1; i <= 3; ++i)
    {
        const float o = (float) i * 5.f;
        g.drawLine ((float) w - o, (float) h, (float) w, (float) h - o, lw);
    }
}
