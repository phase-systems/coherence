// COHERENCE - interactive controls.
// Every control talks to its parameter through a juce::ParameterAttachment (undoable, gesture-correct).
// All of them draw through theme::, so a theme changes how they look and nothing else.

#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace coherence::gui
{
// Tells the editor what is under the mouse, for the readout in the bottom bar.
class HoverSink
{
public:
    virtual ~HoverSink() = default;
    virtual void hoverChanged (juce::RangedAudioParameter* param, const juce::String& fallbackText) = 0;
    // What the readout shows for the thing under the mouse, when it isn't the parameter's own text.
    virtual void hoverValue (std::function<juce::String()>) {}
};

//==============================================================================
class Knob final : public juce::Component
{
public:
    Knob (juce::RangedAudioParameter&, juce::UndoManager*, const juce::String& labelText, theme::KnobLook, HoverSink*);
    ~Knob() override;

    void beginExternalGesture() { attachment.beginGesture(); }
    void endExternalGesture() { attachment.endGesture(); }
    void setEnabledLook (bool e) { if (e != look.enabled) { look.enabled = e; repaint(); } }
    void setLabel (const juce::String& s) { if (s != labelText) { labelText = s; repaint(); } }
    void setValueOverride (std::function<juce::String()> f) { valueOverride = std::move (f); repaint(); }
    // Drag/wheel resolution in parameter units (e.g. 0.1 dB, and 0.01 dB with Shift). 0 = continuous.
    void setValueSteps (float coarse, float fine) { coarseStep = coarse; fineStep = fine; }
    float getCoarseStep() const { return coarseStep; }
    // One wheel notch / step key: `steps` whole steps (fine = the Shift step), as one undoable gesture.
    void stepBy (int steps, bool fine);
    // A drag position (normalised 0..1), quantised to the step like a mouse drag. Part of the current gesture.
    void dragTo (float normalised, bool fine) { setNormalised (normalised, fine); }

    // Drawn as a value in a dot-matrix readout instead of a knob; it drags and types the same way.
    void setReadout (juce::Rectangle<float> face, float pitch, std::function<juce::String (float)> textForValue = {});
    juce::String currentText() const;

    // Called during a user drag/wheel/edit with the new and previous (denormalised) values.
    std::function<void (float newValue, float oldValue)> onUserChange;
    std::function<void()> onGestureStart, onGestureEnd;

    juce::RangedAudioParameter& parameter() { return param; }
    void setValueFromLink (float denormalised);   // set without triggering onUserChange
    void refresh() { repaint(); }

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    static constexpr int boundsW = 92, boundsH = 88;   // room for the printed scale; hitTest keeps it to the knob itself
    static constexpr float centreX = 46.0f, centreY = 40.0f;

private:
    juce::Rectangle<float> captionArea() const;   // click here to type a value
    void setNormalised (float n, bool fine = false);
    void showEditor();

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String labelText;
    theme::KnobLook look;
    HoverSink* hover;
    float coarseStep = 0.0f, fineStep = 0.0f;
    float value = 0.0f;           // denormalised, as last reported by the parameter
    juce::Point<float> lastDrag;
    float dragNorm = 0.0f;
    bool dragging = false;
    std::function<juce::String()> valueOverride;
    std::unique_ptr<juce::TextEditor> editor;
    bool isReadout = false;
    juce::Rectangle<float> readoutFace;
    float readoutPitch = 1.1f;
    std::function<juce::String (float)> readoutText;
};

//==============================================================================
class Fader final : public juce::Component
{
public:
    Fader (juce::RangedAudioParameter&, juce::UndoManager*, HoverSink*);
    // Drag/wheel resolution in dB, as for Knob::setValueSteps (0 = continuous). The OFF position is kept.
    void setValueSteps (float coarse, float fine) { coarseStep = coarse; fineStep = fine; }
    float getCoarseStep() const { return coarseStep; }
    void stepBy (int steps, bool fine);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setSlot (float centreX, float top, float bottom);   // in the parent's coordinates

private:
    float yOfPosition (float normalised) const;
    void setNormalised (float n);

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    HoverSink* hover;
    float value = 0.0f;
    float cx = 0.0f, slotTop = 0.0f, slotBottom = 0.0f;
    float coarseStep = 0.0f, fineStep = 0.0f;
    bool fineDrag = false;
    float dragNorm = 0.0f;
    float lastY = 0.0f;
    bool dragging = false;
};

//==============================================================================
// A console push button: bound to a bool parameter (a toggle) or to a callback.
class ConsoleButton final : public juce::Component
{
public:
    enum class Style { key, letter, plainText, undoIcon, redoIcon, powerKey };

    ConsoleButton (juce::RangedAudioParameter&, juce::UndoManager*, const juce::String& text, HoverSink*);
    ConsoleButton (const juce::String& text, std::function<void()> onClick, std::function<bool()> isOn = {},
                   const juce::String& hint = {}, HoverSink* = nullptr);

    void setStyle (Style s) { style = s; repaint(); }
    void setTextHeight (float h) { textHeight = h; repaint(); }
    void setFace (juce::Rectangle<float> faceInParent);   // the keycap, exact, in the parent's coordinates
    void refresh() { repaint(); }
    bool isLit() const { return isOn(); }
    void trigger();   // what a click does

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    bool isOn() const;

    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::String text, hint;
    Style style = Style::key;
    std::function<void()> click;
    std::function<bool()> on;
    HoverSink* hover = nullptr;
    juce::Rectangle<float> face;
    float textHeight = 9.6f;
    float paramValue = 0.0f;
    bool pressed = false;
};

//==============================================================================
// One key the size of the 4K's BELL key: it reads the current choice, and a click moves to the next.
class CycleKey final : public juce::Component
{
public:
    CycleKey (juce::RangedAudioParameter&, juce::UndoManager*, juce::StringArray labels, HoverSink*);
    void setFace (juce::Rectangle<float> faceInParent);
    int selected() const { return juce::roundToInt (value); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    std::function<void()> onChange;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::StringArray labels;
    HoverSink* hover;
    juce::Rectangle<float> face;
    float value = 0.0f;
};

//==============================================================================
// A two-position rocker for a choice parameter (the Mid/Side selector). Neither side is lit while it's disabled.
class Segmented final : public juce::Component
{
public:
    Segmented (juce::RangedAudioParameter&, juce::UndoManager*, juce::StringArray labels, HoverSink*);
    void setFace (juce::Rectangle<float> faceInParent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void enablementChanged() override { repaint(); }
    int selected() const { return juce::roundToInt (value); }
    std::function<void()> onChange;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::StringArray labels;
    HoverSink* hover;
    juce::Rectangle<float> face;
    float value = 0.0f;
};

//==============================================================================
// Signal-order screen: shows the chain, and swaps EQ and DYN when clicked.
class RoutingDisplay final : public juce::Component
{
public:
    RoutingDisplay (juce::RangedAudioParameter&, juce::UndoManager*, HoverSink*);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    std::function<bool()> eqActive, dynActive;   // to dim bypassed blocks

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    HoverSink* hover;
    float value = 0.0f;
};
} // namespace coherence::gui
