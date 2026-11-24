#include "toastpopup.hpp"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

ToastPopup::ToastPopup(const QIcon &icon, const QString &title, const QString &text, const QString &actionText,
					   QWidget *parent)
	: QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
{
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	setAttribute(Qt::WA_DeleteOnClose);
	setFixedWidth(360);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(16, 14, 16, 14);
	root->setSpacing(12);

	auto *top = new QHBoxLayout();
	top->setSpacing(12);
	auto *iconLabel = new QLabel(this);
	iconLabel->setPixmap(icon.pixmap(32, 32));
	iconLabel->setFixedSize(32, 32);
	auto *column = new QVBoxLayout();
	column->setSpacing(2);
	auto *titleLabel = new QLabel(title, this);
	titleLabel->setStyleSheet("color: #FFFFFF; font-size: 10pt; font-weight: 600; background: transparent;");
	auto *bodyLabel = new QLabel(text, this);
	bodyLabel->setWordWrap(true);
	bodyLabel->setStyleSheet("color: #C8C8C8; font-size: 9pt; background: transparent;");
	column->addWidget(titleLabel);
	column->addWidget(bodyLabel);
	top->addWidget(iconLabel, 0, Qt::AlignTop);
	top->addLayout(column, 1);
	root->addLayout(top);

	auto *buttons = new QHBoxLayout();
	buttons->setSpacing(8);
	const QString base = "QPushButton { border-radius: 4px; padding: 7px 14px; font-size: 9pt; min-height: 18px; "
						 "border: 1px solid #4A4A4A; background: #3A3A3A; color: #FFFFFF; } "
						 "QPushButton:hover { background: #454545; } QPushButton:pressed { background: #303030; }";
	auto *action = new QPushButton(actionText, this);
	action->setCursor(Qt::PointingHandCursor);
	action->setStyleSheet(base);
	auto *dismiss = new QPushButton(tr("Dismiss"), this);
	dismiss->setCursor(Qt::PointingHandCursor);
	dismiss->setStyleSheet(base);
	buttons->addWidget(action, 1);
	buttons->addWidget(dismiss, 1);
	root->addLayout(buttons);

	connect(action, &QPushButton::clicked, this, [this]() {
		emit actionClicked();
		close();
	});
	connect(dismiss, &QPushButton::clicked, this, &QWidget::close);

	m_timer = new QTimer(this);
	m_timer->setSingleShot(true);
	connect(m_timer, &QTimer::timeout, this, &QWidget::close);
}

void ToastPopup::showToast(int milliseconds)
{
	adjustSize();
	const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
	const QPoint target(area.right() - width() - 16, area.bottom() - height() - 16);
	move(target.x() + 60, target.y());
	setWindowOpacity(0.0);
	show();

	auto *slide = new QPropertyAnimation(this, "pos", this);
	slide->setDuration(220);
	slide->setStartValue(QPoint(target.x() + 60, target.y()));
	slide->setEndValue(target);
	slide->setEasingCurve(QEasingCurve::OutCubic);
	auto *fade = new QPropertyAnimation(this, "windowOpacity", this);
	fade->setDuration(220);
	fade->setStartValue(0.0);
	fade->setEndValue(1.0);
	slide->start(QAbstractAnimation::DeleteWhenStopped);
	fade->start(QAbstractAnimation::DeleteWhenStopped);
	m_timer->start(milliseconds);
}

void ToastPopup::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(QColor("#3C3C3C"), 1));
	p.setBrush(QColor("#202020"));
	p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}
