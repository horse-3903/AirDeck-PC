#pragma once

#include <QStringList>

// Virtual Xbox 360 controllers that an earlier run (or another program) left on the ViGEm bus. A leftover pad
// keeps its XInput number, so new pads land on the next one and player 1 becomes controller 2.

// Instance IDs of the virtual Xbox 360 pads that are plugged into the ViGEm bus right now.
QStringList findVirtualPads();

// Removes them. Needs administrator rights. Returns how many were removed.
int removeVirtualPads();

// Runs the removal in a copy of this program that asks Windows for administrator rights. Returns true when it ran.
bool removeVirtualPadsElevated();

// Starts this program again with administrator rights. Returns false when the request was declined.
bool relaunchElevated();

// The argument the elevated copy is started with
constexpr const char *kCleanPadsArgument = "--clean-pads";
