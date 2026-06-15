#pragma once
// Settings.h — portable (UTF-8, Win32-free) settings model for the General page
// (Linux port L4-g). This is the bridge between the on-disk config and the
// portable forms::Panel: a small struct of the General "Behavior" toggles, a
// non-destructive reader/writer over the launcher's JSON config TEXT (it edits
// only the keys it owns, preserving everything else byte-for-byte), and a binder
// that builds a forms::Panel from the model and reads it back.
//
// Crucially the writer never rewrites the whole file — the Windows Config::Save
// owns the full schema. applyGeneral() only replaces the scalar values of the
// keys General owns inside an existing config string, so adopting this on Linux
// can't drop fields the portable model doesn't know about. The KAT
// (settings_selfcheck) locks parse + round-trip-preserves-other-keys + the panel
// binding on both platforms.

#include "Forms.h"

#include <string>

namespace settings {

// The General page's behavior toggles (defaults match AppConfig's defaults).
struct General {
    bool startFullscreen    = false;
    bool minimizeOnLaunch   = true;
    bool defenderExclusions = false;
    bool discordRichPresence = true;
    int  downloadLimitKBps  = 0;   // 0 = unlimited
};

// Read the General fields out of a launcher config JSON string. Missing keys keep
// the struct's defaults (so configs predating a field are handled gracefully).
General parseGeneral(const std::string& json);

// Return `json` with the scalar value of each General-owned key replaced to match
// `g`. Keys absent from `json` are left absent (this only edits what's there);
// every other byte of `json` is preserved. Pure string op — no file I/O.
std::string applyGeneral(const std::string& json, const General& g);

// Build a forms::Panel (checkbox rows + a download-limit field) reflecting `g`,
// laid out within `bounds`. Field keys match the General struct members.
forms::Panel buildGeneralPanel(const General& g, const platform::Rect& bounds);

// Read a panel built by buildGeneralPanel back into a General (parsing the
// download-limit text; non-numeric or negative clamps to 0).
General readGeneralPanel(const forms::Panel& p);

// ── File-backed General (portable) ───────────────────────────────────────────
// The launcher config path for the current platform: <data_dir>/config.json
// (Windows: %LOCALAPPDATA%\ArcadeLauncher; Linux: XDG data dir). UTF-8.
std::string configPath();

// Load the General fields from the config file at `path`. Returns false (and
// leaves `out` at its defaults) when the file is missing or empty; the General
// keys themselves still fall back to defaults individually if absent.
bool loadGeneralFromFile(const std::string& path, General& out);

// Write `g` back into the config file at `path` *non-destructively*: the existing
// file is read, only the General-owned scalar keys are rewritten (applyGeneral),
// and the result is written via a temp file + replace so a crash can't truncate
// the real config. Returns false without touching anything when the file is
// missing (the full schema is owned by the Windows Config::Save, which creates
// it) or the temp write/replace fails.
bool saveGeneralToFile(const std::string& path, const General& g);

} // namespace settings
