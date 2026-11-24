#include "controllertester.hpp"

#include "slotcolors.hpp"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace
{
constexpr quint32 B_START = 0x1, B_VIEW = 0x2, B_A = 0x4, B_B = 0x8, B_X = 0x10, B_Y = 0x20,
				  B_UP = 0x40, B_DOWN = 0x80, B_LEFT = 0x100, B_RIGHT = 0x200, B_L1 = 0x400,
				  B_R1 = 0x800, B_L3 = 0x1000, B_R3 = 0x2000;
}

ControllerTester::ControllerTester(QWidget *parent) : QWidget(parent)
{
	setMinimumSize(minimumSizeHint());
}

void ControllerTester::setSlot(int slot)
{
	m_slot = slot;
	update();
}

void ControllerTester::setState(quint32 held, float lx, float ly, float rx, float ry, float lt,
								float rt)
{
	if (held == m_held && lx == m_lx && ly == m_ly && rx == m_rx && ry == m_ry && lt == m_lt &&
		rt == m_rt)
		return;
	m_held = held;
	m_lx = lx;
	m_ly = ly;
	m_rx = rx;
	m_ry = ry;
	m_lt = lt;
	m_rt = rt;
	update();
}

void ControllerTester::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	const QColor accent = slotColor(m_slot);
	const QColor idle("#1C1C1C");
	const QColor edge("#34343A");
	const QColor text("#8A8A8A");

	// Work in a 16 x 9 grid centred in the widget
	const double u = std::min(width() / 16.0, height() / 9.0);
	const QPointF origin((width() - 16 * u) / 2, (height() - 9 * u) / 2);
	const auto P = [&](double x, double y) { return QPointF(origin.x() + x * u, origin.y() + y * u); };
	const auto R = [&](double x, double y, double w, double h) {
		return QRectF(P(x, y), QSizeF(w * u, h * u));
	};
	const auto lit = [&](quint32 bit) { return (m_held & bit) != 0; };

	QFont font = p.font();
	font.setPixelSize(std::max(10, static_cast<int>(0.42 * u)));
	p.setFont(font);

	// Body
	p.setPen(QPen(edge, std::max(1.0, 0.06 * u)));
	p.setBrush(QColor("#0E0E10"));
	p.drawRoundedRect(R(1.2, 1.9, 13.6, 6.6), 1.6 * u, 1.6 * u);

	// Triggers (analog) and shoulders
	const auto trigger = [&](double x, float value, const QString &label) {
		p.setPen(QPen(edge, 1));
		p.setBrush(idle);
		p.drawRoundedRect(R(x, 0.2, 3.4, 0.7), 0.2 * u, 0.2 * u);
		p.setPen(Qt::NoPen);
		p.setBrush(accent);
		p.drawRoundedRect(R(x, 0.2, 3.4 * std::clamp(value, 0.0f, 1.0f), 0.7), 0.2 * u, 0.2 * u);
		p.setPen(text);
		p.drawText(R(x, 0.95, 3.4, 0.5), Qt::AlignCenter, QString("%1  %2").arg(label).arg(value, 0, 'f', 2));
	};
	trigger(2.3, m_lt, "L2");
	trigger(10.3, m_rt, "R2");
	const auto shoulder = [&](double x, quint32 bit, const QString &label) {
		p.setPen(QPen(lit(bit) ? accent : edge, 1.5));
		p.setBrush(lit(bit) ? accent : idle);
		p.drawRoundedRect(R(x, 1.45, 3.4, 0.55), 0.25 * u, 0.25 * u);
		p.setPen(lit(bit) ? QColor("#000000") : text);
		p.drawText(R(x, 1.45, 3.4, 0.55), Qt::AlignCenter, label);
	};
	shoulder(2.3, B_L1, "L1");
	shoulder(10.3, B_R1, "R1");

	// Sticks
	const auto stick = [&](QPointF center, float x, float y, quint32 clickBit) {
		const double radius = 1.5 * u;
		p.setPen(QPen(lit(clickBit) ? accent : edge, 1.5));
		p.setBrush(idle);
		p.drawEllipse(center, radius, radius);
		const bool moved = std::hypot(x, y) > 0.04;
		p.setPen(Qt::NoPen);
		p.setBrush(moved ? accent : QColor("#4A4A52"));
		p.drawEllipse(center + QPointF(x * radius * 0.6, y * radius * 0.6), 0.55 * u, 0.55 * u);
		p.setPen(text);
		p.drawText(QRectF(center.x() - 1.6 * u, center.y() + 1.55 * u, 3.2 * u, 0.5 * u), Qt::AlignCenter,
				   QString("%1, %2").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2));
	};
	stick(P(4.4, 4.2), m_lx, m_ly, B_L3);
	stick(P(10.4, 6.0), m_rx, m_ry, B_R3);

	// D-pad
	const auto pad = [&](double dx, double dy, quint32 bit) {
		const bool on = lit(bit);
		p.setPen(QPen(on ? accent : edge, 1.5));
		p.setBrush(on ? accent : idle);
		p.drawRoundedRect(R(5.75 + dx - 0.45, 6.15 + dy - 0.45, 0.9, 0.9), 0.15 * u, 0.15 * u);
	};
	pad(0, -0.95, B_UP);
	pad(0, 0.95, B_DOWN);
	pad(-0.95, 0, B_LEFT);
	pad(0.95, 0, B_RIGHT);

	// Face buttons
	const auto face = [&](double dx, double dy, quint32 bit, const QString &label) {
		const bool on = lit(bit);
		p.setPen(QPen(on ? accent : edge, 1.5));
		p.setBrush(on ? accent : idle);
		p.drawEllipse(P(12.0 + dx, 3.9 + dy), 0.58 * u, 0.58 * u);
		p.setPen(on ? QColor("#000000") : text);
		p.drawText(QRectF(P(12.0 + dx - 0.58, 3.9 + dy - 0.58), QSizeF(1.16 * u, 1.16 * u)), Qt::AlignCenter, label);
	};
	face(0, -1.05, B_Y, "Y");
	face(0, 1.05, B_A, "A");
	face(-1.05, 0, B_X, "X");
	face(1.05, 0, B_B, "B");

	// Select and Start
	const auto small = [&](double x, quint32 bit, const QString &label) {
		const bool on = lit(bit);
		p.setPen(QPen(on ? accent : edge, 1.5));
		p.setBrush(on ? accent : idle);
		p.drawRoundedRect(R(x, 3.6, 1.1, 0.5), 0.25 * u, 0.25 * u);
		p.setPen(text);
		p.drawText(R(x - 0.3, 4.15, 1.7, 0.5), Qt::AlignCenter, label);
	};
	small(6.55, B_VIEW, "Select");
	small(8.35, B_START, "Start");
}
