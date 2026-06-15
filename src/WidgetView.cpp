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

namespace {
constexpr float kTextPadX = 8.0f;  // inner horizontal padding for text fields
}

void drawTextEdit(platform::IRenderer2D& r, const widgets::TextEdit& t, const Theme& th) {
    r.fillRoundedRect(t.bounds, th.radius, t.enabled ? th.normal : th.disabled);

    const float textLeft = t.bounds.x + kTextPadX;
    const float ty = t.bounds.y + (t.bounds.h - th.fontPx) * 0.5f;

    if (th.font) {
        // Selection highlight (drawn under the text).
        if (widgets::hasSelection(t) && t.enabled) {
            const size_t lo = t.caret < t.anchor ? t.caret : t.anchor;
            const size_t hi = t.caret < t.anchor ? t.anchor : t.caret;
            const float xlo = textLeft + r.measureText(th.font, t.text.substr(0, lo));
            const float xhi = textLeft + r.measureText(th.font, t.text.substr(0, hi));
            Color sel = th.accent; sel.a = 0.35f;
            r.fillRect({xlo, t.bounds.y + 4.0f, xhi - xlo, t.bounds.h - 8.0f}, sel);
        }

        // Clip text to the inner box so a long string can't spill past the border.
        r.pushClip({t.bounds.x + 1.0f, t.bounds.y + 1.0f,
                    t.bounds.w - 2.0f, t.bounds.h - 2.0f});
        if (!t.text.empty())
            r.drawText(th.font, t.text, textLeft, ty,
                       t.enabled ? th.text : th.textDisabled, platform::TextAlign::Left);

        // Caret (only when focused + enabled).
        if (t.focused && t.enabled) {
            const float cx = textLeft + r.measureText(th.font, t.text.substr(0, t.caret));
            r.drawLine(cx, t.bounds.y + 5.0f, cx, t.bounds.y + t.bounds.h - 5.0f,
                       th.text, 1.0f);
        }
        r.popClip();
    }

    const bool emphasize = t.focused && t.enabled;
    r.strokeRoundedRect(t.bounds, th.radius, emphasize ? th.accent : th.border,
                        emphasize ? 1.5f : 1.0f);
}

size_t caretIndexFromX(platform::IRenderer2D& r, const widgets::TextEdit& t,
                       const Theme& th, float xPx) {
    if (!th.font || t.text.empty()) return t.text.empty() ? 0 : t.text.size();
    const float target = xPx - (t.bounds.x + kTextPadX);
    if (target <= 0) return 0;
    // Walk char boundaries; return the boundary whose glyph midpoint the click
    // falls before (so clicking the left half of a glyph lands before it).
    size_t i = 0;
    float prevW = 0.0f;
    while (i < t.text.size()) {
        const size_t n = widgets::nextCharBoundary(t.text, i);
        const float w = r.measureText(th.font, t.text.substr(0, n));
        if (target < (prevW + w) * 0.5f) return i;
        prevW = w;
        i = n;
    }
    return t.text.size();
}

void drawCombo(platform::IRenderer2D& r, const widgets::Combo& c, const Theme& th,
               const std::string& placeholder) {
    // Header.
    Color hdr;
    switch (widgets::visualState(c)) {
        case widgets::Visual::Hover:    hdr = th.hover;    break;
        case widgets::Visual::Disabled: hdr = th.disabled; break;
        case widgets::Visual::Normal:
        default:                        hdr = th.normal;   break;
    }
    r.fillRoundedRect(c.bounds, th.radius, hdr);
    r.strokeRoundedRect(c.bounds, th.radius, c.open ? th.accent : th.border,
                        c.open ? 1.5f : 1.0f);

    const float padX = 8.0f;
    if (th.font) {
        const std::string sel = widgets::selectedText(c);
        const bool hasSel = !sel.empty();
        const float ty = c.bounds.y + (c.bounds.h - th.fontPx) * 0.5f;
        r.drawText(th.font, hasSel ? sel : placeholder, c.bounds.x + padX, ty,
                   c.enabled ? (hasSel ? th.text : th.textDisabled) : th.textDisabled,
                   platform::TextAlign::Left);

        // Chevron (down caret) on the right.
        const float cxr = c.bounds.x + c.bounds.w - 16.0f;
        const float cyr = c.bounds.y + c.bounds.h * 0.5f;
        const Color chev = c.enabled ? th.text : th.textDisabled;
        r.drawLine(cxr - 4, cyr - 2, cxr, cyr + 3, chev, 1.5f);
        r.drawLine(cxr, cyr + 3, cxr + 4, cyr - 2, chev, 1.5f);
    }

    // Open popup list (caller draws open combos last so this overlays).
    if (c.open && !c.options.empty()) {
        const platform::Rect pop = widgets::comboPopupRect(c);
        // Solid panel + border so it reads as a layer above the page.
        r.fillRoundedRect(pop, th.radius, th.normal);
        for (int i = 0; i < static_cast<int>(c.options.size()); ++i) {
            const platform::Rect row = widgets::comboItemRect(c, i);
            if (i == c.highlight) {
                Color hi = th.accent; hi.a = 0.30f;
                r.fillRect(row, hi);
            }
            if (th.font) {
                const float ty = row.y + (row.h - th.fontPx) * 0.5f;
                r.drawText(th.font, c.options[static_cast<size_t>(i)],
                           row.x + padX, ty, th.text, platform::TextAlign::Left);
            }
        }
        r.strokeRoundedRect(pop, th.radius, th.border, 1.0f);
    }
}

void drawListBox(platform::IRenderer2D& r, const widgets::ListBox& l, const Theme& th) {
    r.fillRoundedRect(l.bounds, th.radius, l.enabled ? th.normal : th.disabled);

    const float ih = widgets::listItemHeight(l);
    const float padX = 8.0f;

    // Clip rows to the inner box so partial top/bottom rows are cut cleanly.
    r.pushClip({l.bounds.x + 1.0f, l.bounds.y + 1.0f,
                l.bounds.w - 2.0f, l.bounds.h - 2.0f});
    for (int i = 0; i < static_cast<int>(l.options.size()); ++i) {
        const float ry = l.bounds.y - l.scroll + ih * static_cast<float>(i);
        if (ry + ih < l.bounds.y || ry > l.bounds.y + l.bounds.h) continue;  // off-screen
        const Rect row{l.bounds.x, ry, l.bounds.w, ih};
        if (i == l.selected) {
            Color sel = th.accent; sel.a = 0.55f;
            r.fillRect(row, sel);
        } else if (i == l.highlight && l.enabled) {
            r.fillRect(row, th.hover);
        }
        if (th.font)
            r.drawText(th.font, l.options[static_cast<size_t>(i)],
                       l.bounds.x + padX, ry + (ih - th.fontPx) * 0.5f,
                       l.enabled ? th.text : th.textDisabled, platform::TextAlign::Left);
    }
    r.popClip();

    // Scrollbar thumb (only when content overflows).
    const float maxS = widgets::listMaxScroll(l);
    if (maxS > 0.0f) {
        const float trackH = l.bounds.h - 4.0f;
        const float content = widgets::listContentHeight(l);
        const float thumbH = trackH * (l.bounds.h / content);
        const float thumbY = l.bounds.y + 2.0f + (trackH - thumbH) * (l.scroll / maxS);
        const float thumbW = 4.0f;
        const Rect thumb{l.bounds.x + l.bounds.w - thumbW - 2.0f, thumbY, thumbW, thumbH};
        Color t = th.border; t.a = 0.9f;
        r.fillRoundedRect(thumb, thumbW * 0.5f, t);
    }

    r.strokeRoundedRect(l.bounds, th.radius, th.border, 1.0f);
}

} // namespace widgetview
