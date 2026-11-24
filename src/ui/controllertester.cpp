#include "controllertester.hpp"

#include "slotcolors.hpp"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>
#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>
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

void ControllerTester::setPs4(bool ps4)
{
	m_ps4 = ps4;
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

void ControllerTester::drawDs4(QPainter &p)
{
	static QByteArray tmpl;
	if (tmpl.isEmpty())
	{
		QFile file(":/controllers/ds4-live.svg");
		if (file.open(QIODevice::ReadOnly))
			tmpl = file.readAll();
	}
	if (tmpl.isEmpty())
		return;

	const QColor accent = slotColor(m_slot);
	const QString idle = "#1c1c21";
	QByteArray data = tmpl;
	const auto setFill = [&](const char *token, bool on, const QColor &color) {
		data.replace(QByteArray("{{") + token + "}}", (on ? color.name() : idle).toUtf8());
	};
	const auto lit = [&](quint32 bit) { return (m_held & bit) != 0; };
	setFill("l1", lit(B_L1), accent);
	setFill("r1", lit(B_R1), accent);
	setFill("touchpad", false, accent);
	setFill("up", lit(B_UP), accent);
	setFill("down", lit(B_DOWN), accent);
	setFill("left", lit(B_LEFT), accent);
	setFill("right", lit(B_RIGHT), accent);
	setFill("triangle", lit(B_Y), QColor("#3fb68b"));
	setFill("cross", lit(B_A), QColor("#3e8ef7"));
	setFill("square", lit(B_X), QColor("#d96ba6"));
	setFill("circle", lit(B_B), QColor("#e5484d"));
	setFill("share", lit(B_VIEW), accent);
	setFill("options", lit(B_START), accent);
	setFill("l3", lit(B_L3), accent);
	setFill("r3", lit(B_R3), accent);
	setFill("ps", false, accent);
	const auto move = [](float x, float y) { return QString("translate(%1 %2)").arg(x * 14, 0, 'f', 2).arg(y * 14, 0, 'f', 2); };
	data.replace("{{lstick}}", move(m_lx, m_ly).toUtf8());
	data.replace("{{rstick}}", move(m_rx, m_ry).toUtf8());

	// The artwork is 580 x 388; fit it into the 100 x 72 design space
	const double s = std::min(100.0 / 580.0, 72.0 / 388.0);
	const QSizeF drawn(580 * s, 388 * s);
	const QRectF target((100 - drawn.width()) / 2, (72 - drawn.height()) / 2, drawn.width(), drawn.height());
	QSvgRenderer renderer(data);
	renderer.render(&p, target);

	// L2 and R2 sit above the bumpers and fill as they are pulled (artwork units mapped by hand)
	const auto toDesign = [&](double x, double y) { return QPointF(target.x() + (x - 10) * s, target.y() + (y + 4) * s); };
	const auto trigger = [&](double x, float value) {
		const QPointF topLeft = toDesign(x, 1);
		const QSizeF size(60 * s, 22 * s);
		const QRectF rect(topLeft, size);
		p.setPen(QPen(QColor("#33333a"), 0.5));
		p.setBrush(QColor("#17171b"));
		p.drawRoundedRect(rect, 2, 2);
		const double v = std::clamp(value, 0.0f, 1.0f);
		if (v > 0.01)
		{
			p.save();
			QPainterPath clip;
			clip.addRoundedRect(rect, 2, 2);
			p.setClipPath(clip);
			p.setPen(Qt::NoPen);
			p.setBrush(accent);
			p.drawRect(QRectF(rect.x(), rect.bottom() - rect.height() * v, rect.width(), rect.height() * v));
			p.restore();
		}
	};
	trigger(104, m_lt);
	trigger(436, m_rt);
}

void ControllerTester::drawController(QPainter &p)
{
	if (m_ps4)
	{
		drawDs4(p);
		return;
	}
	const QColor accent = slotColor(m_slot);
	const auto lit = [&](quint32 bit) { return (m_held & bit) != 0; };
	const QColor bodyFill("#0E0E10"), panel("#17171A"), edge("#2A2A30"), faint("#3A3A41");
	const QTransform mirror(-1, 0, 0, 1, 100, 0);

	// Triggers are the horns above the shoulders and fill from the tip down as they are pulled
	const auto horn = [&](bool right, float value) {
		QPainterPath path;
		path.moveTo(17, 14);
		path.cubicTo(17.5, 7, 20.5, 1.2, 26, 1.2);
		path.cubicTo(29, 1.2, 30.4, 6, 30.4, 14);
		path.closeSubpath();
		if (right)
			path = mirror.map(path);
		p.setPen(QPen(edge, 0.5));
		p.setBrush(panel);
		p.drawPath(path);
		const double v = std::clamp(value, 0.0f, 1.0f);
		if (v > 0.01)
		{
			p.save();
			p.setClipPath(path);
			p.setPen(Qt::NoPen);
			p.setBrush(accent);
			p.drawRect(QRectF(0, 1.2 + (1 - v) * 12.8, 100, 14));
			p.restore();
		}
	};
	horn(false, m_lt);
	horn(true, m_rt);

	// Body
	QPainterPath body;
	if (!m_ps4)
	{
		body.moveTo(24, 18);
		body.cubicTo(30, 15, 70, 15, 76, 18);
		body.cubicTo(88, 18, 97, 32, 98, 50);
		body.cubicTo(99, 62, 92, 70, 85, 66);
		body.cubicTo(80, 62, 76, 52, 66, 50);
		body.lineTo(34, 50);
		body.cubicTo(24, 52, 20, 62, 15, 66);
		body.cubicTo(8, 70, 1, 62, 2, 50);
		body.cubicTo(3, 32, 12, 18, 24, 18);
	}
	else
	{
		body.moveTo(22, 20);
		body.cubicTo(35, 15, 65, 15, 78, 20);
		body.cubicTo(92, 22, 99, 38, 98, 52);
		body.cubicTo(97, 64, 90, 70, 83, 64);
		body.cubicTo(78, 59, 74, 52, 66, 50);
		body.lineTo(34, 50);
		body.cubicTo(26, 52, 22, 59, 17, 64);
		body.cubicTo(10, 70, 3, 64, 2, 52);
		body.cubicTo(1, 38, 8, 22, 22, 20);
	}
	p.setPen(QPen(edge, 0.6));
	p.setBrush(bodyFill);
	p.drawPath(body);

	// Bumpers along the shoulders
	const auto bumper = [&](bool right, quint32 bit) {
		QPainterPath path;
		path.moveTo(12, 25);
		path.cubicTo(14, 17, 22, 13.6, 35, 14.6);
		path.lineTo(39, 19);
		path.cubicTo(30, 18, 20, 19.4, 14.6, 28);
		path.closeSubpath();
		if (right)
			path = mirror.map(path);
		const bool on = lit(bit);
		p.setPen(QPen(on ? accent : edge, 0.5));
		p.setBrush(on ? accent : panel);
		p.drawPath(path);
	};
	bumper(false, B_L1);
	bumper(true, B_R1);

	// Sticks: a ring, the cap moves with the input and the ring lights when it is clicked
	const auto stick = [&](QPointF c, float x, float y, quint32 clickBit, double r) {
		const bool clicked = lit(clickBit);
		p.setPen(QPen(clicked ? accent : edge, clicked ? 1.1 : 0.6));
		p.setBrush(QColor("#08080A"));
		p.drawEllipse(c, r, r);
		const bool moved = std::hypot(x, y) > 0.04;
		p.setPen(QPen(moved ? accent : faint, 0.5));
		p.setBrush(moved ? QColor(accent.red(), accent.green(), accent.blue(), 90) : panel);
		p.drawEllipse(c + QPointF(x * r * 0.42, y * r * 0.42), r * 0.66, r * 0.66);
	};

	// D-pad: a cross whose arms light up
	const auto dpad = [&](QPointF c) {
		p.setPen(QPen(edge, 0.5));
		p.setBrush(QColor("#08080A"));
		p.drawEllipse(c, 8.2, 8.2);
		const auto arm = [&](double dx, double dy, quint32 bit) {
			const bool on = lit(bit);
			p.setPen(QPen(on ? accent : faint, 0.4));
			p.setBrush(on ? accent : panel);
			p.drawRoundedRect(QRectF(c.x() + dx - 2.1, c.y() + dy - 2.1, 4.2, 4.2), 0.9, 0.9);
		};
		arm(0, -4.2, B_UP);
		arm(0, 4.2, B_DOWN);
		arm(-4.2, 0, B_LEFT);
		arm(4.2, 0, B_RIGHT);
		p.setPen(Qt::NoPen);
		p.setBrush(panel);
		p.drawRect(QRectF(c.x() - 2.1, c.y() - 2.1, 4.2, 4.2));
	};

	// Small buttons (view / menu, share / options)
	const auto pill = [&](QPointF c, quint32 bit, int glyph) {
		const bool on = lit(bit);
		p.setPen(QPen(on ? accent : edge, 0.5));
		p.setBrush(on ? accent : panel);
		p.drawEllipse(c, 2.5, 2.5);
		p.setPen(QPen(on ? QColor("#000000") : faint, 0.45));
		p.setBrush(Qt::NoBrush);
		if (glyph == 0)
		{
			p.drawRect(QRectF(c.x() - 1.3, c.y() - 0.8, 1.7, 1.7));
			p.drawRect(QRectF(c.x() - 0.4, c.y() - 1.5, 1.7, 1.7));
		}
		else
		{
			for (int i = -1; i <= 1; ++i)
				p.drawLine(QPointF(c.x() - 1.2, c.y() + i * 0.85), QPointF(c.x() + 1.2, c.y() + i * 0.85));
		}
	};

	if (!m_ps4)
	{
		// Guide button glows in the slot colour
		p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 120), 0.6));
		p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 60));
		p.drawEllipse(QPointF(50, 22), 4.6, 4.6);
		p.setPen(Qt::NoPen);
		p.setBrush(accent);
		p.drawEllipse(QPointF(50, 22), 2.6, 2.6);

		stick(QPointF(27, 31), m_lx, m_ly, B_L3, 8.2);
		stick(QPointF(62, 45), m_rx, m_ry, B_R3, 8.2);
		dpad(QPointF(38, 45));
		pill(QPointF(44, 27), B_VIEW, 0);
		pill(QPointF(56, 27), B_START, 1);

		QFont font = p.font();
		font.setPixelSize(3);
		font.setBold(true);
		p.setFont(font);
		const auto face = [&](double dx, double dy, quint32 bit, const QColor &color, const QString &label) {
			const bool on = lit(bit);
			const QPointF c(75 + dx, 31 + dy);
			if (on)
			{
				p.setPen(Qt::NoPen);
				p.setBrush(QColor(color.red(), color.green(), color.blue(), 70));
				p.drawEllipse(c, 5.2, 5.2);
			}
			p.setPen(QPen(on ? color : edge, 0.6));
			p.setBrush(on ? color : panel);
			p.drawEllipse(c, 3.5, 3.5);
			p.setPen(on ? QColor("#000000") : faint);
			p.drawText(QRectF(c.x() - 3.5, c.y() - 3.5, 7, 7), Qt::AlignCenter, label);
		};
		face(0, -5.6, B_Y, kYellow, "Y");
		face(0, 5.6, B_A, kGreen, "A");
		face(-5.6, 0, B_X, kBlue, "X");
		face(5.6, 0, B_B, kRed, "B");
		return;
	}

	// DualShock 4: light bar, touchpad, centred sticks, symbols on the face buttons
	p.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 200), 0.8, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(QPointF(34, 17.6), QPointF(66, 17.6));
	p.setPen(QPen(edge, 0.5));
	p.setBrush(panel);
	p.drawRoundedRect(QRectF(37, 22, 26, 12), 2.5, 2.5);
	stick(QPointF(36, 44), m_lx, m_ly, B_L3, 7);
	stick(QPointF(64, 44), m_rx, m_ry, B_R3, 7);
	dpad(QPointF(21, 31));
	pill(QPointF(30, 22), B_VIEW, 0);
	pill(QPointF(70, 22), B_START, 1);
	p.setPen(Qt::NoPen);
	p.setBrush(panel);
	p.drawEllipse(QPointF(50, 40), 1.8, 1.8);

	const QColor psTri("#3FB68B"), psCircle("#E5484D"), psCross("#3E8EF7"), psSquare("#D96BA6");
	const auto symbol = [&](double dx, double dy, quint32 bit, const QColor &color, int kind) {
		const bool on = lit(bit);
		const QPointF c(79 + dx, 31 + dy);
		if (on)
		{
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(color.red(), color.green(), color.blue(), 70));
			p.drawEllipse(c, 5.2, 5.2);
		}
		p.setPen(QPen(on ? color : edge, 0.6));
		p.setBrush(on ? QColor(color.red(), color.green(), color.blue(), 90) : panel);
		p.drawEllipse(c, 3.5, 3.5);
		p.setPen(QPen(on ? color : faint, 0.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.setBrush(Qt::NoBrush);
		switch (kind)
		{
		case 0:
			p.drawLine(QPointF(c.x() - 1.4, c.y() - 1.4), QPointF(c.x() + 1.4, c.y() + 1.4));
			p.drawLine(QPointF(c.x() + 1.4, c.y() - 1.4), QPointF(c.x() - 1.4, c.y() + 1.4));
			break;
		case 1:
			p.drawEllipse(c, 1.5, 1.5);
			break;
		case 2:
			p.drawRect(QRectF(c.x() - 1.4, c.y() - 1.4, 2.8, 2.8));
			break;
		default:
		{
			QPolygonF tri;
			tri << QPointF(c.x(), c.y() - 1.7) << QPointF(c.x() + 1.6, c.y() + 1.2) << QPointF(c.x() - 1.6, c.y() + 1.2);
			p.drawPolygon(tri);
		}
		}
	};
	symbol(0, -5.6, B_Y, psTri, 3);
	symbol(0, 5.6, B_A, psCross, 0);
	symbol(-5.6, 0, B_X, psSquare, 2);
	symbol(5.6, 0, B_B, psCircle, 1);
}
