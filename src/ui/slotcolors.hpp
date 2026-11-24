#pragma once

#include <QColor>
#include <algorithm>

// One colour per controller slot, shared by the slot cards, the tester and the phone apps
inline QColor slotColor(int slot)
{
	static const char *colors[] = {"#E5484D", "#3E8EF7", "#F5C518", "#30A46C", "#F97316", "#A855F7"};
	return QColor(colors[std::clamp(slot, 1, 6) - 1]);
}
