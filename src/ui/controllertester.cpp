#include "controllertester.hpp"

#include "slotcolors.hpp"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace
{
constexpr quint32 B_START = 0x1, B_VIEW = 0x2, B_A = 0x4, B_B = 0x8, B_X = 0x10, B_Y = 0x20,
				  B_UP = 0x40, B_DOWN = 0x80, B_LEFT = 0x100, B_RIGHT = 0x200, B_L1 = 0x400,
				  B_R1 = 0x800, B_L3 = 0x1000, B_R3 = 0x2000;

const QColor kIdle("#17171A");
const QColor kEdge("#34343A");
const QColor kDim("#8A8A92");
const QColor kText("#D8D8DE");
const QColor kGreen("#30A46C"), kRed("#E5484D"), kBlue("#3E8EF7"), kYellow("#F5C518");
} // namespace

ControllerTester::ControllerTester(QWidget *parent) : QWidget(parent)
{
	setMinimumSize(minimumSizeHint());
}

void ControllerTester::setCompact(bool compact)
{
	m_compact = compact;
	setMinimumSize(compact ? QSize(80, 60) : minimumSizeHint());
	update();
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
	p.setRenderHint(QPainter::TextAntialiasing);

	const double W = width(), H = height();
	if (m_compact)
	{
		const double s = std::min(W / 100.0, H / 72.0);
		p.translate((W - 100 * s) / 2, (H - 72 * s) / 2);
		p.scale(s, s);
		drawController(p);
		return;
	}
	const double gap = 28;
	const double leftW = std::min(W * 0.40, 360.0);

	// Left: live readouts, drawn in a 300 x 300 design space
	{
		const double s = std::min(leftW / 300.0, H / 300.0);
		p.save();
		p.translate(0, (H - 300 * s) / 2);
		p.scale(s, s);
		drawReadouts(p);
		p.restore();
	}
	// Right: the controller itself, drawn in a 100 x 72 design space
	{
		const double rw = W - leftW - gap, rh = H;
		const double s = std::min(rw / 100.0, rh / 72.0);
		p.save();
		p.translate(leftW + gap + (rw - 100 * s) / 2, (rh - 72 * s) / 2);
		p.scale(s, s);
		drawController(p);
		p.restore();
	}
}

void ControllerTester::drawReadouts(QPainter &p)
{
	const QColor accent = slotColor(m_slot);
	const auto lit = [&](quint32 bit) { return (m_held & bit) != 0; };

	QFont header = p.font();
	header.setPixelSize(10);
	header.setBold(true);
	header.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
	QFont body = p.font();
	body.setPixelSize(11);
	body.setBold(false);
	QFont mono("Consolas");
	mono.setPixelSize(11);

	const auto section = [&](double y, const QString &title) {
		p.setFont(header);
		p.setPen(kDim);
		p.drawText(QRectF(0, y, 300, 14), Qt::AlignLeft | Qt::AlignVCenter, title);
	};

	// Buttons
	section(0, "BUTTONS");
	struct Chip
	{
		const char *label;
		quint32 bit;
	};
	static const Chip chips[] = {{"A", B_A},       {"B", B_B},         {"X", B_X},        {"Y", B_Y},
								 {"LB", B_L1},     {"RB", B_R1},       {"View", B_VIEW},  {"Menu", B_START},
								 {"L3", B_L3},     {"R3", B_R3},       {"▲", B_UP},  {"▼", B_DOWN},
								 {"◀", B_LEFT}, {"▶", B_RIGHT}};
	p.setFont(body);
	for (int i = 0; i < 14; ++i)
	{
		const QRectF r(4 + (i % 7) * 41.0, 20 + (i / 7) * 28.0, 37, 24);
		const bool on = lit(chips[i].bit);
		p.setPen(QPen(on ? accent : kEdge, 1.2));
		p.setBrush(on ? accent : kIdle);
		p.drawRoundedRect(r, 6, 6);
		p.setPen(on ? QColor("#000000") : kText);
		p.drawText(r, Qt::AlignCenter, chips[i].label);
	}

	// Triggers
	section(84, "TRIGGERS");
	const auto bar = [&](double y, const QString &label, float value) {
		p.setFont(body);
		p.setPen(kText);
		p.drawText(QRectF(0, y, 26, 14), Qt::AlignLeft | Qt::AlignVCenter, label);
		const QRectF track(30, y + 1, 200, 12);
		p.setPen(QPen(kEdge, 1));
		p.setBrush(kIdle);
		p.drawRoundedRect(track, 6, 6);
		const double fill = std::clamp(value, 0.0f, 1.0f);
		if (fill > 0.01)
		{
			p.setPen(Qt::NoPen);
			p.setBrush(accent);
			p.drawRoundedRect(QRectF(track.x(), track.y(), std::max(12.0, track.width() * fill), track.height()), 6, 6);
		}
		p.setFont(mono);
		p.setPen(kDim);
		p.drawText(QRectF(238, y, 62, 14), Qt::AlignLeft | Qt::AlignVCenter, QString::number(value, 'f', 2));
	};
	bar(104, "LT", m_lt);
	bar(124, "RT", m_rt);

	// Sticks
	section(152, "STICKS");
	const auto stick = [&](double cx, float x, float y, bool pressed, const QString &name) {
		const double cy = 214, r = 46;
		p.setPen(QPen(pressed ? accent : kEdge, pressed ? 2.0 : 1.2));
		p.setBrush(kIdle);
		p.drawEllipse(QPointF(cx, cy), r, r);
		p.setPen(QPen(kEdge, 0.8, Qt::DotLine));
		p.drawLine(QPointF(cx - r, cy), QPointF(cx + r, cy));
		p.drawLine(QPointF(cx, cy - r), QPointF(cx, cy + r));
		p.drawEllipse(QPointF(cx, cy), r * 0.2, r * 0.2);
		const bool moved = std::hypot(x, y) > 0.04;
		p.setPen(Qt::NoPen);
		p.setBrush(moved ? accent : QColor("#55555C"));
		p.drawEllipse(QPointF(cx + x * (r - 9), cy + y * (r - 9)), 8, 8);
		p.setFont(mono);
		p.setPen(kDim);
		p.drawText(QRectF(cx - 70, cy + r + 6, 140, 14), Qt::AlignCenter,
				   QString("%1  %2, %3").arg(name).arg(x, 5, 'f', 2).arg(y, 5, 'f', 2));
	};
	stick(70, m_lx, m_ly, lit(B_L3), "L");
	stick(230, m_rx, m_ry, lit(B_R3), "R");
}

void ControllerTester::drawController(QPainter &p)
{
	const QColor accent = slotColor(m_slot);
	const auto lit = [&](quint32 bit) { return (m_held & bit) != 0; };

	QFont font = p.font();
	font.setPixelSize(3);
	font.setBold(true);
	p.setFont(font);

	// Body
	QPainterPath body;
	body.moveTo(24, 18);
	body.cubicTo(30, 15, 70, 15, 76, 18);
	body.cubicTo(88, 18, 97, 32, 98, 50);
	body.cubicTo(99, 62, 92, 70, 85, 66);
	body.cubicTo(80, 62, 76, 52, 66, 50);
	body.lineTo(34, 50);
	body.cubicTo(24, 52, 20, 62, 15, 66);
	body.cubicTo(8, 70, 1, 62, 2, 50);
	body.cubicTo(3, 32, 12, 18, 24, 18);
	p.setPen(QPen(kEdge, 0.7));
	p.setBrush(QColor("#111114"));
	p.drawPath(body);

	// Triggers fill from the left, bumpers sit under them
	const auto trigger = [&](double x, float value) {
		p.setPen(QPen(kEdge, 0.5));
		p.setBrush(kIdle);
		p.drawRoundedRect(QRectF(x, 2, 20, 4.5), 2, 2);
		const double fill = std::clamp(value, 0.0f, 1.0f);
		if (fill > 0.01)
		{
			p.setPen(Qt::NoPen);
			p.setBrush(accent);
			p.drawRoundedRect(QRectF(x, 2, 20 * fill, 4.5), 2, 2);
		}
	};
	trigger(14, m_lt);
	trigger(66, m_rt);
	const auto bumper = [&](double x, quint32 bit) {
		const bool on = lit(bit);
		p.setPen(QPen(on ? accent : kEdge, 0.6));
		p.setBrush(on ? accent : kIdle);
		p.drawRoundedRect(QRectF(x, 8.5, 22, 5), 2.5, 2.5);
	};
	bumper(12, B_L1);
	bumper(66, B_R1);

	// Sticks
	const auto stick = [&](QPointF c, float x, float y, quint32 clickBit) {
		p.setPen(QPen(lit(clickBit) ? accent : kEdge, lit(clickBit) ? 1.0 : 0.6));
		p.setBrush(kIdle);
		p.drawEllipse(c, 7.5, 7.5);
		const bool moved = std::hypot(x, y) > 0.04;
		p.setPen(Qt::NoPen);
		p.setBrush(moved ? accent : QColor("#3A3A40"));
		p.drawEllipse(c + QPointF(x * 3.2, y * 3.2), 5.2, 5.2);
	};
	stick(QPointF(27, 31), m_lx, m_ly, B_L3);
	stick(QPointF(62, 45), m_rx, m_ry, B_R3);

	// D-pad
	const auto arm = [&](double dx, double dy, quint32 bit) {
		const bool on = lit(bit);
		p.setPen(QPen(on ? accent : kEdge, 0.5));
		p.setBrush(on ? accent : kIdle);
		p.drawRoundedRect(QRectF(38 + dx - 2.4, 45 + dy - 2.4, 4.8, 4.8), 1, 1);
	};
	arm(0, -4.6, B_UP);
	arm(0, 4.6, B_DOWN);
	arm(-4.6, 0, B_LEFT);
	arm(4.6, 0, B_RIGHT);
	p.setPen(Qt::NoPen);
	p.setBrush(kIdle);
	p.drawRect(QRectF(36.4, 43.4, 3.2, 3.2));

	// Face buttons in the Xbox colours
	const auto face = [&](double dx, double dy, quint32 bit, const QColor &color, const QString &label) {
		const bool on = lit(bit);
		const QPointF c(74 + dx, 31 + dy);
		p.setPen(QPen(on ? color : kEdge, on ? 0.9 : 0.6));
		p.setBrush(on ? color : kIdle);
		p.drawEllipse(c, 3.6, 3.6);
		p.setPen(on ? QColor("#000000") : color);
		p.drawText(QRectF(c.x() - 3.6, c.y() - 3.6, 7.2, 7.2), Qt::AlignCenter, label);
	};
	face(0, -5.2, B_Y, kYellow, "Y");
	face(0, 5.2, B_A, kGreen, "A");
	face(-5.2, 0, B_X, kBlue, "X");
	face(5.2, 0, B_B, kRed, "B");

	// View and Menu with their usual symbols
	const auto small = [&](QPointF c, quint32 bit, bool menu) {
		const bool on = lit(bit);
		p.setPen(QPen(on ? accent : kEdge, 0.5));
		p.setBrush(on ? accent : kIdle);
		p.drawEllipse(c, 2.6, 2.6);
		p.setPen(QPen(on ? QColor("#000000") : kDim, 0.5));
		if (menu)
		{
			for (int i = -1; i <= 1; ++i)
				p.drawLine(QPointF(c.x() - 1.3, c.y() + i * 0.9), QPointF(c.x() + 1.3, c.y() + i * 0.9));
		}
		else
		{
			p.setBrush(Qt::NoBrush);
			p.drawRect(QRectF(c.x() - 1.4, c.y() - 0.9, 1.9, 1.9));
			p.drawRect(QRectF(c.x() - 0.5, c.y() - 1.6, 1.9, 1.9));
		}
	};
	small(QPointF(44, 27), B_VIEW, false);
	small(QPointF(56, 27), B_START, true);

	// Guide button
	p.setPen(QPen(kEdge, 0.5));
	p.setBrush(kIdle);
	p.drawEllipse(QPointF(50, 22), 3, 3);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 160));
	p.drawEllipse(QPointF(50, 22), 1.3, 1.3);
}
