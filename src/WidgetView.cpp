// WidgetView.cpp — portable widget drawing (Linux port L4-a), IRenderer2D only.

#include "WidgetView.h"

namespace widgetview {

using platform::Color;
using platform::Rect;

Theme darkTheme() {
    Theme t;
    // GitHub-dark button palette: a calm surface that lifts on hover and sinks
    // when pressed; the accent-tinted border keeps it legible on the dark app bg.
    t.normal       = {0.20f, 0.22f, 0.26f, 1.0f};
    t.hover        = {0.26f, 0.29f, 0.34f, 1.0f};
    t.pressed      = {0.15f, 0.17f, 0.20f, 1.0f};
    t.disabled     = {0.16f, 0.17f, 0.19f, 1.0f};
    t.border       = {0.33f, 0.36f, 0.41f, 1.0f};
    t.accent       = {0.23f, 0.51f, 0.96f, 1.0f};  // GitHub-blue checked fill
    t.text         = {0.90f, 0.92f, 0.95f, 1.0f};
    t.textDisabled = {0.45f, 0.47f, 0.50f, 1.0f};
    t.font   = 0;
    t.fontPx = 15.0f;
    t.radius = 6.0f;
    return t;
}

void drawButton(platform::IRenderer2D& r, const widgets::Button& b, const Theme& th) {
    Color fill, label;
    switch (widgets::visualState(b)) {
        case widgets::Visual::Hover:    fill = th.hover;    label = th.text;         break;
        case widgets::Visual::Pressed:  fill = th.pressed;  label = th.text;         break;
        case widgets::Visual::Disabled: fill = th.disabled; label = th.textDisabled; break;
        case widgets::Visual::Normal:
        default:                        fill = th.normal;   label = th.text;         break;
    }

    r.fillRoundedRect(b.bounds, th.radius, fill);
    r.strokeRoundedRect(b.bounds, th.radius, th.border, 1.0f);

    if (th.font && !b.label.empty()) {
        // nanovg aligns text to its top, so center vertically by offsetting half
        // the leftover height; center horizontally via TextAlign::Center.
        const float cx = b.bounds.x + b.bounds.w * 0.5f;
        const float ty = b.bounds.y + (b.bounds.h - th.fontPx) * 0.5f;
        r.drawText(th.font, b.label, cx, ty, label, platform::TextAlign::Center);
    }
}

void drawCheckbox(platform::IRenderer2D& r, const widgets::Checkbox& c, const Theme& th) {
    const widgets::Visual vs = widgets::visualState(c);

    // Square box: side = min(20, bounds.h), vertically centered, left-aligned.
    const float side = c.bounds.h < 20.0f ? c.bounds.h : 20.0f;
    const float bx = c.bounds.x;
    const float by = c.bounds.y + (c.bounds.h - side) * 0.5f;
    const Rect box{bx, by, side, side};

    Color boxFill;
    switch (vs) {
        case widgets::Visual::Hover:    boxFill = th.hover;    break;
        case widgets::Visual::Pressed:  boxFill = th.pressed;  break;
        case widgets::Visual::Disabled: boxFill = th.disabled; break;
        case widgets::Visual::Normal:
        default:                        boxFill = th.normal;   break;
    }

    if (c.checked && c.enabled) {
        // Accent-filled box with a checkmark drawn as two strokes.
        r.fillRoundedRect(box, 4.0f, th.accent);
        const float x0 = bx + side * 0.24f, y0 = by + side * 0.52f;
        const float x1 = bx + side * 0.43f, y1 = by + side * 0.72f;
        const float x2 = bx + side * 0.78f, y2 = by + side * 0.30f;
        const Color tick{1.0f, 1.0f, 1.0f, 1.0f};
        r.drawLine(x0, y0, x1, y1, tick, 2.0f);
        r.drawLine(x1, y1, x2, y2, tick, 2.0f);
    } else {
        r.fillRoundedRect(box, 4.0f, boxFill);
        if (c.checked) {  // checked but disabled: muted tick, no accent fill
            const Color tick = th.textDisabled;
            const float x0 = bx + side * 0.24f, y0 = by + side * 0.52f;
            const float x1 = bx + side * 0.43f, y1 = by + side * 0.72f;
            const float x2 = bx + side * 0.78f, y2 = by + side * 0.30f;
            r.drawLine(x0, y0, x1, y1, tick, 2.0f);
            r.drawLine(x1, y1, x2, y2, tick, 2.0f);
        }
    }
    r.strokeRoundedRect(box, 4.0f, th.border, 1.0f);

    if (th.font && !c.label.empty()) {
        const float lx = bx + side + 8.0f;
        const float ty = c.bounds.y + (c.bounds.h - th.fontPx) * 0.5f;
        const Color label = c.enabled ? th.text : th.textDisabled;
        r.drawText(th.font, c.label, lx, ty, label, platform::TextAlign::Left);
    }
}

} // namespace widgetview
