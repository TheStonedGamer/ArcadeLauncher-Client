// Forms.cpp — portable settings-panel logic (Linux port L4-f). See Forms.h.
#include "Forms.h"

namespace forms {

// ── Row construction ─────────────────────────────────────────────────────────
int addCheckbox(Panel& p, const std::string& key, const std::string& label, bool checked) {
    Field f; f.kind = Kind::Checkbox; f.key = key;
    f.checkbox.label = label; f.checkbox.checked = checked;
    p.fields.push_back(std::move(f));
    return (int)p.fields.size() - 1;
}
int addTextEdit(Panel& p, const std::string& key, const std::string& label, const std::string& text) {
    Field f; f.kind = Kind::TextEdit; f.key = key; f.label = label;
    f.textedit.text = text; f.textedit.caret = text.size(); f.textedit.anchor = text.size();
    p.fields.push_back(std::move(f));
    return (int)p.fields.size() - 1;
}
int addCombo(Panel& p, const std::string& key, const std::string& label,
             const std::vector<std::string>& options, int selected) {
    Field f; f.kind = Kind::Combo; f.key = key; f.label = label;
    f.combo.options = options; f.combo.selected = selected;
    p.fields.push_back(std::move(f));
    return (int)p.fields.size() - 1;
}
int addButton(Panel& p, const std::string& key, const std::string& label) {
    Field f; f.kind = Kind::Button; f.key = key; f.label = label;
    f.button.label = label;
    p.fields.push_back(std::move(f));
    return (int)p.fields.size() - 1;
}

// ── Geometry ───────────────────────────────────────────────────────────────
// The control rect for row `i`: checkbox rows span the inset row width; the
// other kinds occupy the right column past `labelWidth`.
static Rect controlRect(const Panel& p, int i) {
    const Field& f = p.fields[(size_t)i];
    const float rowY = p.bounds.y + i * p.rowHeight;
    const float cy   = rowY + (p.rowHeight - p.controlH) * 0.5f;
    if (f.kind == Kind::Checkbox)
        return {p.bounds.x + p.padX, cy, p.bounds.w - 2 * p.padX, p.controlH};
    const float cx = p.bounds.x + p.labelWidth;
    return {cx, cy, p.bounds.w - p.labelWidth - p.padX, p.controlH};
}

void layout(Panel& p) {
    for (int i = 0; i < (int)p.fields.size(); ++i) {
        Field& f = p.fields[(size_t)i];
        const Rect r = controlRect(p, i);
        switch (f.kind) {
            case Kind::Checkbox: f.checkbox.bounds = r; break;
            case Kind::TextEdit: f.textedit.bounds = r; break;
            case Kind::Combo:    f.combo.bounds    = r; break;
            case Kind::Button:   f.button.bounds   = r; break;
        }
    }
}

int fieldAt(const Panel& p, float x, float y) {
    // An open combo's popup overlays the rows below it, so it gets first refusal.
    for (int i = 0; i < (int)p.fields.size(); ++i) {
        const Field& f = p.fields[(size_t)i];
        if (f.kind == Kind::Combo && f.combo.open &&
            widgets::contains(widgets::comboPopupRect(f.combo), x, y))
            return i;
    }
    for (int i = 0; i < (int)p.fields.size(); ++i)
        if (widgets::contains(controlRect(p, i), x, y)) return i;
    return -1;
}

// ── Focus ────────────────────────────────────────────────────────────────────
static bool fieldEnabled(const Field& f) {
    switch (f.kind) {
        case Kind::Checkbox: return f.checkbox.enabled;
        case Kind::TextEdit: return f.textedit.enabled;
        case Kind::Combo:    return f.combo.enabled;
        case Kind::Button:   return f.button.enabled;
    }
    return false;
}

void setFocus(Panel& p, int index) {
    p.focused = index;
    for (int i = 0; i < (int)p.fields.size(); ++i) {
        Field& f = p.fields[(size_t)i];
        if (f.kind == Kind::TextEdit) f.textedit.focused = (i == index);
        if (f.kind == Kind::Combo && i != index) f.combo.open = false;  // others close
    }
}

static void focusStep(Panel& p, int dir) {
    const int n = (int)p.fields.size();
    if (n == 0) return;
    int start = (p.focused >= 0 && p.focused < n) ? p.focused : (dir > 0 ? -1 : 0);
    for (int k = 0; k < n; ++k) {
        start = (start + dir + n) % n;
        if (fieldEnabled(p.fields[(size_t)start])) { setFocus(p, start); return; }
    }
}
void focusNext(Panel& p) { focusStep(p, +1); }
void focusPrev(Panel& p) { focusStep(p, -1); }

// ── Event routing ──────────────────────────────────────────────────────────
static widgets::InputResult routeToField(Field& f, const platform::Event& ev) {
    switch (f.kind) {
        case Kind::Checkbox: return widgets::handle(f.checkbox, ev);
        case Kind::TextEdit: return widgets::handle(f.textedit, ev);
        case Kind::Combo:    return widgets::handle(f.combo, ev);
        case Kind::Button:   return widgets::handle(f.button, ev);
    }
    return {};
}

PanelResult handle(Panel& p, const platform::Event& ev) {
    using platform::EventType;
    PanelResult res;

    const bool isMouse = ev.type == EventType::MouseMove ||
                         ev.type == EventType::MouseDown ||
                         ev.type == EventType::MouseUp   ||
                         ev.type == EventType::MouseWheel;
    if (isMouse) {
        // If a combo is open and the cursor is over its popup, only that combo
        // should react (its dropdown visually covers the rows beneath it).
        int popupOwner = -1;
        for (int i = 0; i < (int)p.fields.size(); ++i) {
            const Field& f = p.fields[(size_t)i];
            if (f.kind == Kind::Combo && f.combo.open &&
                widgets::contains(widgets::comboPopupRect(f.combo), ev.x, ev.y)) {
                popupOwner = i; break;
            }
        }
        if (ev.type == EventType::MouseDown) {
            int tgt = (popupOwner >= 0) ? popupOwner : fieldAt(p, ev.x, ev.y);
            if (tgt >= 0) setFocus(p, tgt);
        }
        for (int i = 0; i < (int)p.fields.size(); ++i) {
            if (popupOwner >= 0 && i != popupOwner) continue;
            widgets::InputResult r = routeToField(p.fields[(size_t)i], ev);
            if (r.consumed) res.consumed = true;
            if (r.clicked)  res.clicked  = i;
        }
        return res;
    }

    // Tab traversal.
    if (ev.type == EventType::KeyDown && ev.key == platform::Key::Tab) {
        if (ev.shift) focusPrev(p); else focusNext(p);
        res.consumed = true;
        return res;
    }

    // Everything else goes to the focused field.
    if (p.focused < 0 || p.focused >= (int)p.fields.size()) return res;
    Field& f = p.fields[(size_t)p.focused];
    if (!fieldEnabled(f)) return res;

    // Keyboard activation the bare widgets don't provide: Space toggles a focused
    // checkbox; Enter/Space "clicks" a focused button; Enter/Space opens a closed
    // focused combo (so the form is keyboard-drivable end to end).
    if (ev.type == EventType::KeyDown) {
        if (f.kind == Kind::Checkbox && ev.key == platform::Key::Space) {
            f.checkbox.checked = !f.checkbox.checked;
            res.consumed = true; res.clicked = p.focused; return res;
        }
        if (f.kind == Kind::Button &&
            (ev.key == platform::Key::Enter || ev.key == platform::Key::Space)) {
            res.consumed = true; res.clicked = p.focused; return res;
        }
        if (f.kind == Kind::Combo && !f.combo.open &&
            (ev.key == platform::Key::Enter || ev.key == platform::Key::Space)) {
            f.combo.open = true;
            if (f.combo.highlight < 0)
                f.combo.highlight = f.combo.selected >= 0 ? f.combo.selected : 0;
            res.consumed = true; return res;
        }
    }

    widgets::InputResult r = routeToField(f, ev);
    if (r.consumed) res.consumed = true;
    if (r.clicked)  res.clicked  = p.focused;
    return res;
}

// ── Readback ─────────────────────────────────────────────────────────────────
const Field* find(const Panel& p, const std::string& key) {
    for (const Field& f : p.fields) if (f.key == key) return &f;
    return nullptr;
}
Field* find(Panel& p, const std::string& key) {
    for (Field& f : p.fields) if (f.key == key) return &f;
    return nullptr;
}
bool boolValue(const Panel& p, const std::string& key, bool def) {
    const Field* f = find(p, key);
    return (f && f->kind == Kind::Checkbox) ? f->checkbox.checked : def;
}
std::string textValue(const Panel& p, const std::string& key, const std::string& def) {
    const Field* f = find(p, key);
    return (f && f->kind == Kind::TextEdit) ? f->textedit.text : def;
}
int choiceValue(const Panel& p, const std::string& key, int def) {
    const Field* f = find(p, key);
    return (f && f->kind == Kind::Combo) ? f->combo.selected : def;
}

} // namespace forms
