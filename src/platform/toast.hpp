#pragma once

#include <QString>

// Windows notifications (the same ones other apps send to the notification centre), with one action button.
// The button re-launches this exe with "vgp-stop:now", which the running instance picks up.

constexpr const char *kStopServerArgument = "vgp-stop:now";

// Registers the app identity and the vgp-stop: link so Windows accepts our toasts. Call once at startup.
void initToastIdentity();

// Returns false when Windows would not take the toast, so the caller can fall back.
bool showNativeToast(const QString &title, const QString &text, const QString &actionLabel);
