#pragma once

#include <QString>

// The debug log (debug.log in the app data folder). It never grows without limit and never logs about itself:
//  - one file, rotated at a size cap, with one older copy kept
//  - an identical line repeated back to back is written once with a repeat count
//  - a cap on lines per second, with a summary of how many were dropped
//  - long lines are cut
//  - writing never calls back into Qt's logging, so it cannot feed on itself
namespace DebugLog
{
// Switched by the Debug mode setting. Reading it is cheap enough to do for every log message.
void setEnabled(bool enabled);
bool enabled();

QString path();

// A line with a timestamp in front. Does nothing unless debug mode is on, or force is set.
void write(const QString &line, bool force = false);

// A line that already carries its own timestamp, such as one formatted by the message handler
void writeRaw(const QString &line);
} // namespace DebugLog
