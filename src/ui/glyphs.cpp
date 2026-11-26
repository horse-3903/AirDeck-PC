#include "glyphs.hpp"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>
#include <QPainterPath>
#include <algorithm>

QPixmap glyphPixmap(Glyph kind, const QColor &color, int size, qreal dpr)
{
	QPixmap pm(QSize(size, size) * dpr);
	pm.setDevicePixelRatio(dpr);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.scale(size / 24.0, size / 24.0);
	QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);

	switch (kind)
	{
	case Glyph::Phone:
		p.drawRoundedRect(QRectF(7, 2.5, 10, 19), 2.2, 2.2);
		p.drawLine(QPointF(10.5, 18.5), QPointF(13.5, 18.5));
		break;
	case Glyph::Browser:
		p.drawRoundedRect(QRectF(3, 4.5, 18, 15), 2.2, 2.2);
		p.drawLine(QPointF(3, 9.5), QPointF(21, 9.5));
		p.drawPoint(QPointF(6.2, 7));
		p.drawPoint(QPointF(8.6, 7));
		break;
	case Glyph::Gamepad:
	{
		QPainterPath body;
		body.moveTo(7, 7.5);
		body.lineTo(17, 7.5);
		body.cubicTo(20.5, 7.5, 22, 11, 22, 15);
		body.cubicTo(22, 18, 20, 19, 18.5, 17.8);
		body.cubicTo(17.3, 16.8, 16.6, 15.5, 15, 15.5);
		body.lineTo(9, 15.5);
		body.cubicTo(7.4, 15.5, 6.7, 16.8, 5.5, 17.8);
		body.cubicTo(4, 19, 2, 18, 2, 15);
		body.cubicTo(2, 11, 3.5, 7.5, 7, 7.5);
		p.drawPath(body);
		p.drawLine(QPointF(7, 10.2), QPointF(7, 13.2));
		p.drawLine(QPointF(5.5, 11.7), QPointF(8.5, 11.7));
		p.drawPoint(QPointF(16, 10.8));
		p.drawPoint(QPointF(18, 12.6));
		break;
	}
	case Glyph::Keyboard:
		p.drawRoundedRect(QRectF(2.5, 6, 19, 12), 2.2, 2.2);
		for (int i = 0; i < 4; ++i)
			p.drawPoint(QPointF(6.3 + i * 3.8, 10));
		p.drawLine(QPointF(7, 14.2), QPointF(17, 14.2));
		break;
	case Glyph::Gear:
		p.drawEllipse(QPointF(12, 12), 3.2, 3.2);
		for (int i = 0; i < 8; ++i)
		{
			p.save();
			p.translate(12, 12);
			p.rotate(i * 45);
			p.drawLine(QPointF(0, -6.4), QPointF(0, -8.6));
			p.restore();
		}
		break;
	}
	return pm;
}

QPixmap controllerPixmap(bool ps4, const QColor &color, const QSize &size, qreal dpr)
{
	QPixmap pm(size * dpr);
	pm.setDevicePixelRatio(dpr);
	pm.fill(Qt::transparent);
	QFile file(ps4 ? ":/controllers/ps4.svg" : ":/controllers/xbox.svg");
	if (!file.open(QIODevice::ReadOnly))
		return pm;
	QByteArray data = file.readAll();
	data.replace("currentColor", color.name().toUtf8());
	QSvgRenderer renderer(data);
	// The artwork is 200 x 148, fit it in the middle of the requested size
	const double s = std::min(size.width() / 200.0, size.height() / 148.0);
	const QSizeF drawn(200 * s, 148 * s);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	renderer.render(&p, QRectF((size.width() - drawn.width()) / 2, (size.height() - drawn.height()) / 2,
							   drawn.width(), drawn.height()));
	return pm;
}
