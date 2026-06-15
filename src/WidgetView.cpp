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

} // namespace widgetview
