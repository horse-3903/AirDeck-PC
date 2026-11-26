#pragma once

#include <QColor>
#include <QPixmap>

enum class Glyph
{
	Phone,
	Browser,
	Gamepad,
	Keyboard,
	Gear
};

// Small line icons drawn in code so they follow the colour and the screen scale.
QPixmap glyphPixmap(Glyph kind, const QColor &color, int size, qreal devicePixelRatio = 1.0);

// Outline of an Xbox or PlayStation style controller
QPixmap controllerPixmap(bool ps4, const QColor &color, const QSize &size, qreal dpr);
