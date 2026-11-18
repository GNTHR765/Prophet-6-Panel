#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** Matte black panel, cream knobs with black skirts, white silkscreen text. */
class MoogLook : public juce::LookAndFeel_V4
{
public:
    MoogLook();

    static const juce::Colour panel, panelLight, rule, ink, inkDim, cream, creamDark, chrome, accent;
    static const juce::Colour modColour[7];   // LFO 1-3, envelope 1, shape 1-2, envelope 2

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int, int) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

    static juce::Font silk (float size, bool bold = false);
};
