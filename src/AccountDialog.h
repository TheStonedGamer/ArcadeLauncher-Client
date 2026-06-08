#pragma once
#include "pch.h"
#include "Config.h"

// Account self-service dialogs (#21 / #22). All talk to the server through a
// ServerClient built from `cfg`. When the user changes their password the new
// value is written back into `cfg.password` so the caller can persist it.

// Full account management window: change password + enable/disable TOTP (with a
// scannable QR code during enrollment). Returns true if `cfg` was modified
// (password changed) and should be saved by the caller.
bool ShowAccountDialog(HWND owner, ServerConfig& cfg);

// Forced password change shown right after login when the server reports
// mustChangePassword. Returns true if the password was successfully changed
// (and `cfg.password` updated).
bool ShowForcedPasswordChange(HWND owner, ServerConfig& cfg);
