#pragma once
// Forms.h — portable (UTF-8, Win32-free) settings-panel model (Linux port L4-f).
// The widget set (button/checkbox/textedit/combo/listbox in widgets::) gives us
// the individual controls; this composes them into a real *form*: a vertical
// stack of labeled rows, with keyboard focus + Tab traversal and keyed value
// readback. It mirrors a slice of the Windows SettingsWindow (the General
// "Behavior" checkbox group plus a path edit / backend combo / browse button)
// without any Win32 — so one panel definition drives both platforms and one KAT
// (forms_selfcheck) locks the layout/focus/readback behavior.
//
// As with the rest of the port the LOGIC lives here (pure: layout math, focus
// state machine, event routing, value readback) and the DRAWING lives in
// widgetview (arcade_gui + the Windows build), paired the same way Widgets pairs
// with WidgetView.

#include "Widgets.h"

#include <string>
#include <vector>

namespace forms {

using platform::Rect;

// Which control a row hosts. Only the matching widget member of Field is used.
enum class Kind { Checkbox, TextEdit, Combo, Button };

// One row of the form: a control plus a stable `key` for value readback and a
// `label` drawn to the control's left (checkbox rows carry their own label, so
// their `label` stays empty and the checkbox spans the row).
struct Field {
    Kind        kind;
    std::string key;     // stable identifier for readback (boolValue/textValue/…)
    std::string label;   // left-column label (non-checkbox rows)

    widgets::Checkbox checkbox;
    widgets::TextEdit textedit;
    widgets::Combo    combo;
    widgets::Button   button;
};

// A vertical settings panel. `bounds` is the content area; rows are laid out top
// to bottom at `rowHeight` each, with the control occupying the right column
// (left of it is `labelWidth` of label). `focused` is the row index receiving
// keyboard input (-1 = none). Pure data — call layout() to position the controls.
struct Panel {
    Rect               bounds;
    std::vector<Field> fields;
    int                focused    = -1;
    float              rowHeight  = 40.0f;
    float              labelWidth = 240.0f;
    float              padX       = 12.0f;   // inset of label / gap around control
    float              controlH   = 28.0f;   // control height within a row
};

// Convenience builders for appending rows (return the new field's index).
int addCheckbox(Panel& p, const std::string& key, const std::string& label, bool checked = false);
int addTextEdit(Panel& p, const std::string& key, const std::string& label, const std::string& text = "");
int addCombo(Panel& p, const std::string& key, const std::string& label,
             const std::vector<std::string>& options, int selected = -1);
int addButton(Panel& p, const std::string& key, const std::string& label);

// Assign every field's control bounds from the panel geometry. Idempotent; call
// after building the panel or whenever `bounds`/metrics change. Checkbox rows get
// the full row width (the box draws inside); other rows get the right column.
void layout(Panel& p);

// Row index whose control contains (x, y), or -1. Honors an open combo's popup
// (a click in the dropped list still belongs to that combo's row).
int fieldAt(const Panel& p, float x, float y);

// Focus traversal over enabled fields. focusNext/Prev wrap; both set `focused`
// and sync the per-widget focus/open flags (only the focused TextEdit is
// `focused`; closing combos that lose focus). No-op when there are no enabled
// fields.
void focusNext(Panel& p);
void focusPrev(Panel& p);
void setFocus(Panel& p, int index);

// Feed one event to the panel. Mouse events go to every field (so hover/press
// track) and a MouseDown additionally moves focus to the clicked row. Tab /
// Shift+Tab move focus (consumed). Other keyboard/text events go to the focused
// field; Space toggles a focused checkbox and Enter/Space "clicks" a focused
// button (the keyboard activation the bare widgets don't do on their own).
// Returns the row index that fired a click/commit this event, or -1.
struct PanelResult {
    bool consumed = false;
    int  clicked  = -1;  // field index whose control completed a click/commit
};
PanelResult handle(Panel& p, const platform::Event& ev);

// ── Value readback ─────────────────────────────────────────────────────────
// Look up a field by key; nullptr when absent. The non-const overload lets the
// caller seed values before display.
const Field* find(const Panel& p, const std::string& key);
Field*       find(Panel& p, const std::string& key);

// Typed readback (defaults returned when the key is missing or the wrong kind).
bool         boolValue(const Panel& p, const std::string& key, bool def = false);
std::string  textValue(const Panel& p, const std::string& key, const std::string& def = "");
int          choiceValue(const Panel& p, const std::string& key, int def = -1);

} // namespace forms
