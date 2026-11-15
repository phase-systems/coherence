#include "Controls.h"
#include "../Parameters.h"

namespace coherence::gui
{
using namespace theme;

namespace
{
constexpr float dragPixelsFullRange = 260.0f;
constexpr float fineFactor = 0.1f;

bool isFine (const juce::ModifierKeys& m) { return m.isShiftDown() || m.isCommandDown(); }

void styleEditor (juce::TextEditor& e, float height)
{
    e.setFont (sans (height));
    e.setJustification (juce::Justification::centred);
    e.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0c0d0f));
    e.setColour (juce::TextEditor::textColourId, current().lcdLit);
    e.setColour (juce::TextEditor::outlineColourId, current().rule);
    e.setColour (juce::TextEditor::focusedOutlineColourId, current().rule);
    e.setColour (juce::TextEditor::highlightColourId, current().lcdDim.withAlpha (0.5f));
    e.setColour (juce::CaretComponent::caretColourId, current().lcdLit);
    e.setIndents (2, 0);
    e.setBorder ({ 1, 1, 1, 1 });
}
} // namespace

//==============================================================================
// Knob
Knob::Knob (juce::RangedAudioParameter& p, juce::UndoManager* um, const juce::String& text, KnobLook l, HoverSink* h)
    : param (p),
      attachment (p, [this] (float v) { value = v; repaint(); }, um),
      labelText (text), look (l), hover (h)
{
    attachment.sendInitialUpdate();
}

Knob::~Knob() = default;

void Knob::setReadout (juce::Rectangle<float> f, float pitch, std::function<juce::String (float)> textForValue)
{
    isReadout = true;
    const auto b = f.expanded (3.0f).getSmallestIntegerContainer();
    setBounds (b);
    readoutFace = f.translated ((float) -b.getX(), (float) -b.getY());
    readoutPitch = pitch;
    readoutText = std::move (textForValue);
    repaint();
}

juce::String Knob::currentText() const
{
    if (valueOverride)
        return valueOverride();
    return param.getText (param.convertTo0to1 (value), 32);
}

juce::Rectangle<float> Knob::captionArea() const
{
    return { centreX - 26.0f, centreY + 27.5f, 52.0f, 13.0f };
}

bool Knob::hitTest (int x, int y)
{
    if (isReadout)
        return true;
    const juce::Point<float> p ((float) x, (float) y);
    return p.getDistanceFrom ({ centreX, centreY + 1.0f }) <= theme::knobHitRadius || captionArea().contains (p);
}

void Knob::paint (juce::Graphics& g)
{
    if (isReadout)
    {
        const auto& t = current();
        lcdPanel (g, readoutFace);
        const auto s = lcdString (readoutText ? readoutText (value) : currentText());
        lcdText (g, s, readoutFace.getCentreX() - lcdWidth (s, readoutPitch) * 0.5f, readoutFace.getCentreY(), readoutPitch, look.enabled ? t.lcdLit : t.lcdDim.withAlpha (0.6f));
        return;
    }
    if (! look.enabled)
        g.beginTransparencyLayer (0.45f);
    // dB knobs show the played value (on the step grid when the value came from a host or controller).
    const auto* stepped = dynamic_cast<const SteppedGainParameter*> (&param);
    drawKnob (g, centreX, centreY, look, param.convertTo0to1 (stepped != nullptr ? stepped->effective (value) : value), editor != nullptr ? juce::String() : labelText);
    if (! look.enabled)
        g.endTransparencyLayer();
}

void Knob::setNormalised (float n, bool fine)
{
    const float old = value;
    float v = param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n));
    if (coarseStep > 0.0f)
    {
        const float step = fine ? fineStep : coarseStep;
        v = param.convertFrom0to1 (param.convertTo0to1 (std::round (v / step) * step));
    }
    if (v == old)
        return;
    {
        const ScopedWriteSource src (WriteSource::gui);
        attachment.setValueAsPartOfGesture (v);
    }
    value = param.convertFrom0to1 (param.convertTo0to1 (v));
    repaint();
    if (onUserChange && v != old)
        onUserChange (value, old);
}

void Knob::setValueFromLink (float v)
{
    const ScopedWriteSource src (WriteSource::gui);
    attachment.setValueAsPartOfGesture (v);
    value = v;
    repaint();
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (! isReadout && captionArea().contains (e.position) && ! e.mods.isPopupMenu())
    {
        showEditor();
        return;
    }
    dragging = true;
    dragNorm = param.convertTo0to1 (value);
    lastDrag = e.position;
    attachment.beginGesture();
    if (onGestureStart)
        onGestureStart();
    e.source.enableUnboundedMouseMovement (true);
    if (e.mods.isAltDown())
    {
        setNormalised (param.getDefaultValue());
        dragNorm = param.getDefaultValue();
    }
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const auto d = e.position - lastDrag;
    lastDrag = e.position;
    float delta = (d.x - d.y) / dragPixelsFullRange;
    if (isFine (e.mods))
        delta *= fineFactor;
    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + delta);
    setNormalised (dragNorm, isFine (e.mods));
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    dragging = false;
    e.source.enableUnboundedMouseMovement (false);
    attachment.endGesture();
    if (onGestureEnd)
        onGestureEnd();
    repaint();
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isReadout)
    {
        showEditor();   // a readout types on a double click; Alt-click resets it
        return;
    }
    if (captionArea().contains (e.position))
        return;
    attachment.beginGesture();
    if (onGestureStart) onGestureStart();
    setNormalised (param.getDefaultValue());
    attachment.endGesture();
    if (onGestureEnd) onGestureEnd();
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float delta = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
    if (delta == 0.0f)
        return;
    if (coarseStep > 0.0f)
    {
        // Stepped knobs: whole steps per wheel event (a typical mouse notch is about 0.1).
        const int steps = (delta > 0.0f ? 1 : -1) * juce::jmax (1, juce::roundToInt (std::abs (delta) / 0.1f));
        stepBy (steps, isFine (e.mods));
        return;
    }
    attachment.beginGesture();
    if (onGestureStart) onGestureStart();
    setNormalised (param.convertTo0to1 (value) + delta * (isFine (e.mods) ? 0.005f : 0.04f));
    attachment.endGesture();
    if (onGestureEnd) onGestureEnd();
}

void Knob::stepBy (int steps, bool fine)
{
    if (coarseStep <= 0.0f || steps == 0)
        return;
    attachment.beginGesture();
    if (onGestureStart) onGestureStart();
    const float step = fine ? fineStep : coarseStep;
    const auto& r = param.getNormalisableRange();
    setNormalised (param.convertTo0to1 (juce::jlimit (r.start, r.end, value + (float) steps * step)), fine);
    attachment.endGesture();
    if (onGestureEnd) onGestureEnd();
}

void Knob::mouseEnter (const juce::MouseEvent&)
{
    if (hover == nullptr)
        return;
    hover->hoverValue ([this] { return currentText(); });
    hover->hoverChanged (&param, {});
}

void Knob::mouseExit (const juce::MouseEvent&)
{
    if (hover != nullptr)
        hover->hoverChanged (nullptr, {});
}

void Knob::showEditor()
{
    editor = std::make_unique<juce::TextEditor>();
    styleEditor (*editor, 10.0f);
    editor->setBounds ((isReadout ? readoutFace : captionArea()).toNearestInt());
    editor->setText (param.getText (param.convertTo0to1 (value), 32), false);
    editor->selectAll();
    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();

    auto finish = [this] (bool commit)
    {
        if (editor == nullptr)
            return;
        if (commit)
        {
            const float old = value;
            const float v = param.convertFrom0to1 (param.getValueForText (editor->getText()));
            const ScopedWriteSource src (WriteSource::gui);
            attachment.setValueAsCompleteGesture (v);
            value = v;
            if (onUserChange)
            {
                if (onGestureStart) onGestureStart();
                onUserChange (v, old);
                if (onGestureEnd) onGestureEnd();
            }
        }
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this)]
        {
            if (safe != nullptr) { safe->editor.reset(); safe->repaint(); }
        });
    };
    editor->onReturnKey = [finish] { finish (true); };
    editor->onEscapeKey = [finish] { finish (false); };
    editor->onFocusLost = [finish] { finish (true); };
}

//==============================================================================
// Fader
Fader::Fader (juce::RangedAudioParameter& p, juce::UndoManager* um, HoverSink* h)
    : param (p), attachment (p, [this] (float v) { value = v; repaint(); }, um), hover (h)
{
    attachment.sendInitialUpdate();
}

void Fader::setSlot (float centreX, float top, float bottom)
{
    const juce::Rectangle<float> area (centreX - 20.0f, top - 4.0f, 62.0f, bottom - top + 12.0f);
    const auto b = area.getSmallestIntegerContainer();
    setBounds (b);
    cx = centreX - (float) b.getX();
    slotTop = top - (float) b.getY();
    slotBottom = bottom - (float) b.getY();
    repaint();
}

float Fader::yOfPosition (float norm) const
{
    return slotBottom - 14.0f - norm * (slotBottom - slotTop - 34.0f);
}

void Fader::paint (juce::Graphics& g)
{
    const auto& t = current();
    const float h = slotBottom - slotTop;
    const auto numInk = t.stripInk.withAlpha (0.92f);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRoundedRectangle (cx - 3.6f, slotTop - 1.0f, 7.2f, h + 2.0f, 3.6f);
    g.setColour (juce::Colour (0xff0b0b0c));
    g.fillRoundedRectangle (cx - 2.5f, slotTop, 5.0f, h, 2.5f);

    // The scale, from the fader's own law.
    for (auto [db, name] : std::initializer_list<std::pair<float, const char*>> { { 12.f, "12" }, { 6.f, "6" }, { 0.f, "0" }, { -5.f, "5" }, { -10.f, "10" }, { -20.f, "20" }, { -30.f, "30" }, { -40.f, "40" }, { -60.f, "" } })
    {
        const float y = yOfPosition (FaderLaw::toPos (db));
        if (*name)
            text (g, name, cx + 26.0f, y, sans (10.0f), numInk);
        else
        {
            juce::Path p;
            const float x = cx + 26.0f;
            p.addEllipse (x - 5.5f, y - 2.2f, 5.2f, 4.4f);
            p.addEllipse (x + 0.3f, y - 2.2f, 5.2f, 4.4f);
            g.setColour (numInk);
            g.strokePath (p, juce::PathStrokeType (0.9f));
        }
    }

    const float y = yOfPosition (param.convertTo0to1 (value)), w = 28.0f, ch = 55.0f;
    const juce::Rectangle<float> c (cx - w * 0.5f, y - ch * 0.5f, w, ch);
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 9, { 0, 5 }).drawForRectangle (g, c.toNearestInt());
    g.setColour (juce::Colour (0xff57585a));
    g.fillRoundedRectangle (c.expanded (0.4f), 2.4f);
    juce::ColourGradient b (juce::Colour (0xffb0b0ad), c.getX(), c.getY(), juce::Colour (0xff8b8b89), c.getX(), c.getBottom(), false);
    g.setGradientFill (b);
    g.fillRoundedRectangle (c, 2.2f);
    for (float f : { 0.11f, 0.22f, 0.31f, 0.40f, 0.60f, 0.70f, 0.79f, 0.89f })
    {
        const float ly = c.getY() + f * ch;
        g.setColour (juce::Colour (0xffe3e3df).withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (c.getX() + 0.8f, ly, w - 1.6f, 0.9f));
        g.setColour (juce::Colour (0xff66676a));
        g.fillRect (juce::Rectangle<float> (c.getX() + 0.8f, ly + 0.9f, w - 1.6f, 0.6f));
    }
    g.setColour (juce::Colour (0xff1c1c1d));
    g.fillRoundedRectangle (c.getX() - 0.4f, y - 0.9f, w + 0.8f, 1.8f, 0.6f);
}

void Fader::setNormalised (float n)
{
    float v = param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n));
    const auto& r = param.getNormalisableRange();
    if (coarseStep > 0.0f && v > r.start)   // the OFF position (range start) is never moved onto a step
    {
        const float step = fineDrag ? fineStep : coarseStep;
        v = juce::jlimit (r.start, r.end, std::round (v / step) * step);
        v = param.convertFrom0to1 (param.convertTo0to1 (v));
    }
    {
        const ScopedWriteSource src (WriteSource::gui);
        attachment.setValueAsPartOfGesture (v);
    }
    value = v;
    repaint();
}

void Fader::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    dragNorm = param.convertTo0to1 (value);
    lastY = e.position.y;
    attachment.beginGesture();
    if (e.mods.isAltDown())
    {
        dragNorm = param.getDefaultValue();
        setNormalised (dragNorm);
    }
}

void Fader::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    float delta = (lastY - e.position.y) / juce::jmax (1.0f, slotBottom - slotTop - 34.0f);
    lastY = e.position.y;
    if (isFine (e.mods))
        delta *= fineFactor;
    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + delta);
    fineDrag = isFine (e.mods);
    setNormalised (dragNorm);
}

void Fader::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    attachment.endGesture();
}

void Fader::mouseDoubleClick (const juce::MouseEvent&)
{
    const ScopedWriteSource src (WriteSource::gui);
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void Fader::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float delta = w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    if (delta == 0.0f)
        return;
    if (coarseStep > 0.0f)
    {
        // Whole steps per wheel event, as on the knobs (a typical mouse notch is about 0.1).
        stepBy ((delta > 0.0f ? 1 : -1) * juce::jmax (1, juce::roundToInt (std::abs (delta) / 0.1f)), isFine (e.mods));
        return;
    }
    attachment.beginGesture();
    fineDrag = isFine (e.mods);
    setNormalised (param.convertTo0to1 (value) + delta * (fineDrag ? 0.004f : 0.03f));
    attachment.endGesture();
}

void Fader::stepBy (int steps, bool fine)
{
    if (coarseStep <= 0.0f || steps == 0)
        return;
    attachment.beginGesture();
    fineDrag = fine;
    const auto& r = param.getNormalisableRange();
    const float step = fine ? fineStep : coarseStep;
    setNormalised (param.convertTo0to1 (juce::jlimit (r.start, r.end, value + (float) steps * step)));
    attachment.endGesture();
}

void Fader::mouseEnter (const juce::MouseEvent&) { if (hover) { hover->hoverValue ({}); hover->hoverChanged (&param, {}); } }
void Fader::mouseExit (const juce::MouseEvent&) { if (hover) hover->hoverChanged (nullptr, {}); }

//==============================================================================
// ConsoleButton
ConsoleButton::ConsoleButton (juce::RangedAudioParameter& p, juce::UndoManager* um, const juce::String& t, HoverSink* h)
    : param (&p), text (t), hover (h)
{
    attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float v) { paramValue = v; repaint(); }, um);
    attachment->sendInitialUpdate();
}

ConsoleButton::ConsoleButton (const juce::String& t, std::function<void()> onClick, std::function<bool()> isOnFn,
                              const juce::String& h, HoverSink* sink)
    : text (t), hint (h), click (std::move (onClick)), on (std::move (isOnFn)), hover (sink)
{
}

void ConsoleButton::setFace (juce::Rectangle<float> f)
{
    const auto b = f.expanded (3.0f).getSmallestIntegerContainer();
    setBounds (b);
    face = f.translated ((float) -b.getX(), (float) -b.getY());
    repaint();
}

bool ConsoleButton::isOn() const
{
    if (param != nullptr)
        return paramValue >= 0.5f;
    return on ? on() : false;
}

void ConsoleButton::paint (juce::Graphics& g)
{
    const auto& t = current();
    const auto dim = pressed ? t.barDim.brighter (0.5f) : t.barDim;
    switch (style)
    {
        case Style::letter:
            theme::text (g, text, face.getCentreX(), face.getCentreY(), sans (15.0f), isOn() ? t.abActive : dim);
            break;
        case Style::plainText:
            theme::text (g, text, face.getCentreX(), face.getCentreY(), sans (8.8f), dim);
            break;
        case Style::undoIcon:
        case Style::redoIcon:
        {
            juce::Path p;
            const float cx = face.getCentreX(), cy = face.getCentreY();
            p.startNewSubPath (cx + 6.5f, cy + 5.0f);
            p.lineTo (cx + 6.5f, cy + 0.5f);
            p.quadraticTo (cx + 6.5f, cy - 3.0f, cx + 2.0f, cy - 3.0f);
            p.lineTo (cx - 6.0f, cy - 3.0f);
            p.startNewSubPath (cx - 2.4f, cy - 6.6f);
            p.lineTo (cx - 6.0f, cy - 3.0f);
            p.lineTo (cx - 2.4f, cy + 0.6f);
            if (style == Style::redoIcon)
                p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, cx, cy));
            g.setColour (dim);
            g.strokePath (p, juce::PathStrokeType (1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case Style::powerKey:
        {
            const bool lit = ! isOn();   // the parameter is Bypass: lit while the plug-in is in
            drawKey (g, face, {}, lit);
            const float cx = face.getCentreX(), cy = face.getCentreY(), r = 4.6f;
            juce::Path p;
            p.addCentredArc (cx, cy + 0.4f, r, r, 0.0f, 0.62f, juce::MathConstants<float>::twoPi - 0.62f, true);
            p.startNewSubPath (cx, cy - r - 1.1f);
            p.lineTo (cx, cy - 0.8f);
            g.setColour (lit ? juce::Colour (0xffb65a1e) : juce::Colour (0xff36373a));
            g.strokePath (p, juce::PathStrokeType (1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case Style::key:
            drawKey (g, face, text, isOn(), textHeight);
            if (pressed)
            {
                g.setColour (juce::Colours::black.withAlpha (0.12f));
                g.fillRoundedRectangle (face, 2.2f);
            }
            break;
    }
}

void ConsoleButton::mouseDown (const juce::MouseEvent&) { pressed = true; repaint(); }

void ConsoleButton::mouseUp (const juce::MouseEvent& e)
{
    pressed = false;
    if (getLocalBounds().contains (e.getPosition()))
        trigger();
    repaint();
}

void ConsoleButton::trigger()
{
    if (param != nullptr)
        attachment->setValueAsCompleteGesture (isOn() ? 0.0f : 1.0f);
    else if (click)
        click();
}

void ConsoleButton::mouseEnter (const juce::MouseEvent&)
{
    if (hover == nullptr)
        return;
    hover->hoverValue ({});
    hover->hoverChanged (param, hint);
}

void ConsoleButton::mouseExit (const juce::MouseEvent&)
{
    if (hover != nullptr)
        hover->hoverChanged (nullptr, {});
}

//==============================================================================
// CycleKey
CycleKey::CycleKey (juce::RangedAudioParameter& p, juce::UndoManager* um, juce::StringArray l, HoverSink* h)
    : param (p), attachment (p, [this] (float v) { value = v; repaint(); if (onChange) onChange(); }, um), labels (std::move (l)), hover (h)
{
    attachment.sendInitialUpdate();
}

void CycleKey::setFace (juce::Rectangle<float> f)
{
    const auto b = f.expanded (3.0f).getSmallestIntegerContainer();
    setBounds (b);
    face = f.translated ((float) -b.getX(), (float) -b.getY());
    repaint();
}

void CycleKey::paint (juce::Graphics& g)
{
    const int i = juce::jlimit (0, labels.size() - 1, selected());
    if (labels[i] == "CUT")
    {
        drawKey (g, face, {}, false, 9.4f);
        drawKeyText (g, face, labels[i], 9.4f, juce::Colour (0xff9a2a24));   // CUT reads red, as on the 4K's CUT key
    }
    else
        drawKey (g, face, labels[i], false, 9.4f);
}

void CycleKey::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture ((float) ((selected() + 1) % labels.size()));
}

void CycleKey::mouseEnter (const juce::MouseEvent&)
{
    if (hover == nullptr)
        return;
    hover->hoverValue ({});
    hover->hoverChanged (&param, {});
}

void CycleKey::mouseExit (const juce::MouseEvent&)
{
    if (hover != nullptr)
        hover->hoverChanged (nullptr, {});
}

//==============================================================================
// Segmented
Segmented::Segmented (juce::RangedAudioParameter& p, juce::UndoManager* um, juce::StringArray l, HoverSink* h)
    : param (p), attachment (p, [this] (float v) { value = v; repaint(); if (onChange) onChange(); }, um), labels (std::move (l)), hover (h)
{
    attachment.sendInitialUpdate();
}

void Segmented::setFace (juce::Rectangle<float> f)
{
    const auto b = f.expanded (3.0f).getSmallestIntegerContainer();
    setBounds (b);
    face = f.translated ((float) -b.getX(), (float) -b.getY());
    repaint();
}

void Segmented::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff131417).withAlpha (0.9f));
    g.fillRoundedRectangle (face.expanded (1.4f), 3.0f);
    const bool active = isEnabled();
    drawKey (g, face.withHeight (20.5f), labels[0], active && selected() == 0, 6.9f);
    drawKey (g, face.withTrimmedTop (21.5f), labels[1], active && selected() == 1, 6.9f);
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    attachment.setValueAsCompleteGesture (e.position.y < face.getCentreY() ? 0.0f : 1.0f);
}

void Segmented::mouseEnter (const juce::MouseEvent&)
{
    if (hover == nullptr)
        return;
    hover->hoverValue ({});
    hover->hoverChanged (&param, {});
}

void Segmented::mouseExit (const juce::MouseEvent&)
{
    if (hover != nullptr)
        hover->hoverChanged (nullptr, {});
}

//==============================================================================
// RoutingDisplay
RoutingDisplay::RoutingDisplay (juce::RangedAudioParameter& p, juce::UndoManager* um, HoverSink* h)
    : param (p), attachment (p, [this] (float v) { value = v; repaint(); }, um), hover (h)
{
    attachment.sendInitialUpdate();
}

void RoutingDisplay::paint (juce::Graphics& g)
{
    const auto& t = current();
    const auto d = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour (0xff0c0d0f));
    g.fillRoundedRectangle (d.expanded (1.6f), 6.0f);
    juce::ColourGradient bg (juce::Colour (0xff17331f), d.getCentreX(), d.getCentreY(), juce::Colour (0xff081009), d.getX(), d.getY(), true);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (d, 5.0f);

    const auto lit = t.lcdLit;
    const juce::Colour dim (0xff237a3c);
    const juce::String title ("SIGNAL ORDER");
    lcdText (g, title, d.getCentreX() - lcdWidth (title, 0.95f) * 0.5f, d.getY() + 13.0f, 0.95f, dim);

    const bool dynFirst = juce::roundToInt (value) == 1;
    const bool eqOn = eqActive ? eqActive() : true, dynOn = dynActive ? dynActive() : true;
    const struct { const char* name; bool active; } blocks[] = { { dynFirst ? "DYN" : "EQ", dynFirst ? dynOn : eqOn },
                                                                { dynFirst ? "EQ" : "DYN", dynFirst ? eqOn : dynOn },
                                                                { "M/S", true } };
    const float bx = d.getCentreX() - 34.0f, bw = 68.0f;
    for (int i = 0; i < 3; ++i)
    {
        const juce::Rectangle<float> b (bx, d.getY() + 25.0f + (float) i * 22.0f, bw, 16.0f);
        g.setColour (juce::Colour (0xff133d21));
        g.fillRoundedRectangle (b, 2.0f);
        const juce::String name (blocks[i].name);
        lcdText (g, name, b.getCentreX() - lcdWidth (name, 1.02f) * 0.5f, b.getCentreY(), 1.02f, blocks[i].active ? lit : dim);
        if (i < 2)
        {
            g.setColour (dim);
            g.fillRect (juce::Rectangle<float> (b.getCentreX() - 0.5f, b.getBottom(), 1.0f, 6.0f));
        }
    }
    juce::Path a;
    const float ax = d.getCentreX(), ay = d.getBottom() - 21.0f;
    a.addRectangle (ax - 1.2f, ay, 2.4f, 8.0f);
    a.addTriangle (ax - 5.0f, ay + 8.0f, ax + 5.0f, ay + 8.0f, ax, ay + 14.0f);
    g.setColour (lit);
    g.fillPath (a);
}

void RoutingDisplay::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (juce::roundToInt (value) == 1 ? 0.0f : 1.0f);
}

void RoutingDisplay::mouseEnter (const juce::MouseEvent&)
{
    if (hover == nullptr)
        return;
    hover->hoverValue ({});
    hover->hoverChanged (&param, {});
}

void RoutingDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hover != nullptr)
        hover->hoverChanged (nullptr, {});
}
} // namespace coherence::gui
