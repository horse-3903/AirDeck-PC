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
	const auto move = [](float x, float y) { return QString("translate(%1 %2)").arg(x * 7, 0, 'f', 2).arg(y * 7, 0, 'f', 2); };
	data.replace("{{lstick}}", move(m_lx, m_ly).toUtf8());
	data.replace("{{rstick}}", move(m_rx, m_ry).toUtf8());

	data.replace("{{accent}}", accent.name().toUtf8());
	data.replace("{{lt}}", QByteArray::number(1.0 - std::clamp(m_lt, 0.0f, 1.0f), 'f', 3));
	data.replace("{{rt}}", QByteArray::number(1.0 - std::clamp(m_rt, 0.0f, 1.0f), 'f', 3));

	// The artwork is 580 x 406 (triggers included); fit it into the 100 x 72 design space
	const double s = std::min(100.0 / 580.0, 72.0 / 406.0);
	const QSizeF drawn(580 * s, 406 * s);
	QSvgRenderer renderer(data);
	renderer.render(&p, QRectF((100 - drawn.width()) / 2, (72 - drawn.height()) / 2, drawn.width(), drawn.height()));
}

void ControllerTester::drawXbox(QPainter &p)
{
	static QByteArray tmpl;
	if (tmpl.isEmpty())
	{
		QFile file(":/controllers/xbox-live.svg");
		if (file.open(QIODevice::ReadOnly))
			tmpl = file.readAll();
	}
	if (tmpl.isEmpty())
		return;

	const QColor accent = slotColor(m_slot);
	const QString idle = "#1a1a1a";
	QByteArray data = tmpl;
	const auto lit = [&](quint32 bit) { return (m_held & bit) != 0; };
	const auto setFill = [&](const char *token, bool on, const QColor &color) {
		data.replace(QByteArray("{{") + token + "}}", (on ? color.name() : idle).toUtf8());
	};
	setFill("lb", lit(B_L1), accent);
	setFill("rb", lit(B_R1), accent);
	setFill("up", lit(B_UP), accent);
	setFill("down", lit(B_DOWN), accent);
	setFill("left", lit(B_LEFT), accent);
	setFill("right", lit(B_RIGHT), accent);
	setFill("a", lit(B_A), QColor("#30a46c"));
	setFill("b", lit(B_B), QColor("#e5484d"));
	setFill("x", lit(B_X), QColor("#3e8ef7"));
	setFill("y", lit(B_Y), QColor("#f5c518"));
	setFill("view", lit(B_VIEW), accent);
	setFill("menu", lit(B_START), accent);
	setFill("l3", lit(B_L3), accent);
	setFill("r3", lit(B_R3), accent);
	setFill("guide", false, accent);
	data.replace("{{accent}}", accent.name().toUtf8());
	// Triggers fill from the bottom, so the hard colour stop sits at one minus the pull
	data.replace("{{lt}}", QByteArray::number(1.0 - std::clamp(m_lt, 0.0f, 1.0f), 'f', 3));
	data.replace("{{rt}}", QByteArray::number(1.0 - std::clamp(m_rt, 0.0f, 1.0f), 'f', 3));
	const auto move = [](float x, float y) { return QString("translate(%1 %2)").arg(x * 11, 0, 'f', 2).arg(y * 11, 0, 'f', 2); };
	data.replace("{{lstick}}", move(m_lx, m_ly).toUtf8());
	data.replace("{{rstick}}", move(m_rx, m_ry).toUtf8());

	// The artwork is 750 x 630; fit it into the 100 x 72 design space
	const double s = std::min(100.0 / 750.0, 72.0 / 630.455);
	const QSizeF drawn(750 * s, 630.455 * s);
	QSvgRenderer renderer(data);
	renderer.render(&p, QRectF((100 - drawn.width()) / 2, (72 - drawn.height()) / 2, drawn.width(), drawn.height()));
}

void ControllerTester::drawController(QPainter &p)
{
	if (m_ps4)
		drawDs4(p);
	else
		drawXbox(p);
}
