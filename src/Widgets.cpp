// Widgets.cpp — portable retained widget logic (Linux port L4). Pure of any
// rendering back-end. Button and Checkbox share one press/release contract.

#include "Widgets.h"

#include <algorithm>

namespace widgets {

using platform::EventType;
using platform::MouseButton;

namespace {

// The shared push contract for any control with bounds/enabled/hover/pressed:
// arm on a left-down inside, and on the matching left-up report consumed, plus
// clicked when the release is also inside. Disabled is inert. Returns the result;
// the caller decides what a click *does* (fire, toggle, …).
template <typename W>
InputResult pushContract(W& w, const platform::Event& ev) {
    InputResult res;
    if (!w.enabled) {
        w.hover = false;
        w.pressed = false;
        return res;
    }
    switch (ev.type) {
        case EventType::MouseMove:
            w.hover = contains(w.bounds, ev.x, ev.y);
            break;
        case EventType::MouseDown:
            if (ev.button == MouseButton::Left &&
                contains(w.bounds, ev.x, ev.y)) {
                w.pressed = true;
                w.hover = true;
                res.consumed = true;
            }
            break;
        case EventType::MouseUp:
            if (ev.button == MouseButton::Left && w.pressed) {
                const bool inside = contains(w.bounds, ev.x, ev.y);
                w.pressed = false;
                w.hover = inside;
                res.consumed = true;
                res.clicked = inside;
            }
            break;
        default:
            break;
    }
    return res;
}

template <typename W>
Visual visualOf(const W& w) {
    if (!w.enabled) return Visual::Disabled;
    if (w.pressed && w.hover) return Visual::Pressed;
    if (w.hover) return Visual::Hover;
    return Visual::Normal;
}

} // namespace

InputResult handle(Button& b, const platform::Event& ev) {
    return pushContract(b, ev);
}

Visual visualState(const Button& b) { return visualOf(b); }

InputResult handle(Checkbox& c, const platform::Event& ev) {
    InputResult res = pushContract(c, ev);
    if (res.clicked) c.checked = !c.checked;  // a completed click toggles
    return res;
}

Visual visualState(const Checkbox& c) { return visualOf(c); }

// ── Text edit ────────────────────────────────────────────────────────────────

size_t prevCharBoundary(const std::string& s, size_t i) {
    if (i == 0) return 0;
    if (i > s.size()) i = s.size();
    --i;
    // Skip back over UTF-8 continuation bytes (0b10xxxxxx) to the lead byte.
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return i;
}

size_t nextCharBoundary(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    return i;
}

namespace {

// Ordered selection range [lo, hi).
void selRange(const TextEdit& t, size_t& lo, size_t& hi) {
    lo = t.caret < t.anchor ? t.caret : t.anchor;
    hi = t.caret < t.anchor ? t.anchor : t.caret;
}

// Remove the current selection (if any); leaves caret == anchor at the cut point.
void eraseSelection(TextEdit& t) {
    size_t lo, hi;
    selRange(t, lo, hi);
    if (lo == hi) return;
    t.text.erase(lo, hi - lo);
    t.caret = t.anchor = lo;
}

void insert(TextEdit& t, const std::string& s) {
    eraseSelection(t);
    if (t.caret > t.text.size()) t.caret = t.text.size();
    t.text.insert(t.caret, s);
    t.caret += s.size();
    t.anchor = t.caret;
}

} // namespace

bool hasSelection(const TextEdit& t) { return t.caret != t.anchor; }

std::string selectedText(const TextEdit& t) {
    size_t lo, hi;
    selRange(t, lo, hi);
    return t.text.substr(lo, hi - lo);
}

void setCaret(TextEdit& t, size_t byteIdx, bool extend) {
    if (byteIdx > t.text.size()) byteIdx = t.text.size();
    // Snap to a char boundary: if mid-codepoint, round down to the lead byte.
    if (byteIdx < t.text.size() &&
        (static_cast<unsigned char>(t.text[byteIdx]) & 0xC0) == 0x80)
        byteIdx = prevCharBoundary(t.text, byteIdx);
    t.caret = byteIdx;
    if (!extend) t.anchor = t.caret;
}

InputResult handle(TextEdit& t, const platform::Event& ev) {
    InputResult res;
    if (!t.enabled || !t.focused) return res;

    switch (ev.type) {
        case EventType::TextInput:
            if (!ev.text.empty()) {
                insert(t, ev.text);
                res.consumed = true;
            }
            break;

        case EventType::KeyDown:
            switch (ev.key) {
                case platform::Key::Backspace:
                    if (hasSelection(t)) {
                        eraseSelection(t);
                    } else if (t.caret > 0) {
                        const size_t p = prevCharBoundary(t.text, t.caret);
                        t.text.erase(p, t.caret - p);
                        t.caret = t.anchor = p;
                    }
                    res.consumed = true;
                    break;

                case platform::Key::Delete:
                    if (hasSelection(t)) {
                        eraseSelection(t);
                    } else if (t.caret < t.text.size()) {
                        const size_t n = nextCharBoundary(t.text, t.caret);
                        t.text.erase(t.caret, n - t.caret);
                    }
                    res.consumed = true;
                    break;

                case platform::Key::Left:
                    if (hasSelection(t) && !ev.shift) {
                        size_t lo, hi; selRange(t, lo, hi);
                        t.caret = t.anchor = lo;  // collapse to selection start
                    } else {
                        t.caret = prevCharBoundary(t.text, t.caret);
                        if (!ev.shift) t.anchor = t.caret;
                    }
                    res.consumed = true;
                    break;

                case platform::Key::Right:
                    if (hasSelection(t) && !ev.shift) {
                        size_t lo, hi; selRange(t, lo, hi);
                        t.caret = t.anchor = hi;  // collapse to selection end
                    } else {
                        t.caret = nextCharBoundary(t.text, t.caret);
                        if (!ev.shift) t.anchor = t.caret;
                    }
                    res.consumed = true;
                    break;

                case platform::Key::Home:
                    t.caret = 0;
                    if (!ev.shift) t.anchor = t.caret;
                    res.consumed = true;
                    break;

                case platform::Key::End:
                    t.caret = t.text.size();
                    if (!ev.shift) t.anchor = t.caret;
                    res.consumed = true;
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
    return res;
}

// ── Combo (dropdown) ─────────────────────────────────────────────────────────

float comboItemHeight(const Combo& c) {
    return c.itemHeight > 0.0f ? c.itemHeight : c.bounds.h;
}

Rect comboPopupRect(const Combo& c) {
    const float ih = comboItemHeight(c);
    return Rect{c.bounds.x, c.bounds.y + c.bounds.h, c.bounds.w,
               ih * static_cast<float>(c.options.size())};
}

Rect comboItemRect(const Combo& c, int index) {
    const float ih = comboItemHeight(c);
    return Rect{c.bounds.x, c.bounds.y + c.bounds.h + ih * static_cast<float>(index),
               c.bounds.w, ih};
}

int comboItemAt(const Combo& c, float x, float y) {
    for (int i = 0; i < static_cast<int>(c.options.size()); ++i)
        if (contains(comboItemRect(c, i), x, y)) return i;
    return -1;
}

std::string selectedText(const Combo& c) {
    if (c.selected < 0 || c.selected >= static_cast<int>(c.options.size())) return {};
    return c.options[static_cast<size_t>(c.selected)];
}

Visual visualState(const Combo& c) {
    if (!c.enabled) return Visual::Disabled;
    if (c.open || c.hover) return Visual::Hover;
    return Visual::Normal;
}

InputResult handle(Combo& c, const platform::Event& ev) {
    InputResult res;
    if (!c.enabled) {
        c.open = false;
        c.hover = false;
        c.highlight = -1;
        return res;
    }

    switch (ev.type) {
        case EventType::MouseMove:
            c.hover = contains(c.bounds, ev.x, ev.y);
            if (c.open) {
                const int i = comboItemAt(c, ev.x, ev.y);
                if (i >= 0) c.highlight = i;  // track the hovered row
            }
            break;

        case EventType::MouseDown:
            if (ev.button != MouseButton::Left) break;
            if (!c.open) {
                if (contains(c.bounds, ev.x, ev.y)) {
                    c.open = true;
                    c.highlight = c.selected;  // start on the current choice
                    res.consumed = true;
                }
            } else {
                const int i = comboItemAt(c, ev.x, ev.y);
                if (i >= 0) {                       // commit a row
                    c.selected = i;
                    c.open = false;
                    res.consumed = true;
                    res.clicked = true;
                } else {                            // header again or outside: close
                    c.open = false;
                    res.consumed = contains(c.bounds, ev.x, ev.y);
                }
            }
            break;

        case EventType::KeyDown:
            if (!c.open) break;
            switch (ev.key) {
                case platform::Key::Down:
                    if (!c.options.empty())
                        c.highlight = c.highlight + 1 >= static_cast<int>(c.options.size())
                                          ? static_cast<int>(c.options.size()) - 1
                                          : c.highlight + 1;
                    res.consumed = true;
                    break;
                case platform::Key::Up:
                    c.highlight = c.highlight <= 0 ? 0 : c.highlight - 1;
                    res.consumed = true;
                    break;
                case platform::Key::Enter:
                    if (c.highlight >= 0) {
                        c.selected = c.highlight;
                        res.clicked = true;
                    }
                    c.open = false;
                    res.consumed = true;
                    break;
                case platform::Key::Escape:
                    c.open = false;
                    res.consumed = true;
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
    return res;
}

// ── ListBox ──────────────────────────────────────────────────────────────────

float listItemHeight(const ListBox& l) {
    return l.itemHeight > 0.0f ? l.itemHeight : 24.0f;
}

float listContentHeight(const ListBox& l) {
    return listItemHeight(l) * static_cast<float>(l.options.size());
}

float listMaxScroll(const ListBox& l) {
    return std::max(0.0f, listContentHeight(l) - l.bounds.h);
}

void listClampScroll(ListBox& l) {
    l.scroll = std::clamp(l.scroll, 0.0f, listMaxScroll(l));
}

int listItemAt(const ListBox& l, float x, float y) {
    if (!contains(l.bounds, x, y)) return -1;
    const float local = y - l.bounds.y + l.scroll;
    const int i = static_cast<int>(local / listItemHeight(l));
    if (i < 0 || i >= static_cast<int>(l.options.size())) return -1;
    return i;
}

void listEnsureVisible(ListBox& l, int index) {
    if (index < 0 || index >= static_cast<int>(l.options.size())) return;
    const float ih = listItemHeight(l);
    const float top = ih * static_cast<float>(index);
    const float bot = top + ih;
    if (top < l.scroll) l.scroll = top;
    else if (bot > l.scroll + l.bounds.h) l.scroll = bot - l.bounds.h;
    listClampScroll(l);
}

InputResult handle(ListBox& l, const platform::Event& ev) {
    InputResult res;
    if (!l.enabled) {
        l.hover = false;
        l.highlight = -1;
        return res;
    }
    const int count = static_cast<int>(l.options.size());

    switch (ev.type) {
        case EventType::MouseWheel:
            if (l.hover || contains(l.bounds, ev.x, ev.y)) {
                l.scroll -= ev.wheel * listItemHeight(l);  // one row per notch
                listClampScroll(l);
                res.consumed = true;
            }
            break;

        case EventType::MouseMove:
            l.hover = contains(l.bounds, ev.x, ev.y);
            l.highlight = listItemAt(l, ev.x, ev.y);
            break;

        case EventType::MouseDown:
            if (ev.button == MouseButton::Left) {
                const int i = listItemAt(l, ev.x, ev.y);
                if (i >= 0) {
                    l.selected = i;
                    res.consumed = true;
                    res.clicked = true;
                }
            }
            break;

        case EventType::KeyDown:
            switch (ev.key) {
                case platform::Key::Down:
                    if (count > 0) {
                        l.selected = l.selected + 1 >= count ? count - 1
                                     : (l.selected < 0 ? 0 : l.selected + 1);
                        listEnsureVisible(l, l.selected);
                        res.consumed = true;
                    }
                    break;
                case platform::Key::Up:
                    if (count > 0) {
                        l.selected = l.selected <= 0 ? 0 : l.selected - 1;
                        listEnsureVisible(l, l.selected);
                        res.consumed = true;
                    }
                    break;
                case platform::Key::Home:
                    if (count > 0) { l.selected = 0; listEnsureVisible(l, 0); res.consumed = true; }
                    break;
                case platform::Key::End:
                    if (count > 0) { l.selected = count - 1; listEnsureVisible(l, count - 1); res.consumed = true; }
                    break;
                case platform::Key::Enter:
                    if (l.selected >= 0) { res.clicked = true; res.consumed = true; }
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
    return res;
}

} // namespace widgets
