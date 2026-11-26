#pragma once

#include <QIcon>
#include <QWidget>

class QTimer;

/**
 * A Windows 11 style notification card shown above the taskbar. Qt's tray messages cannot carry buttons,
 * so this draws its own: app icon, title, text and up to two actions.
 */
class ToastPopup final : public QWidget
{
	Q_OBJECT

  public:
	ToastPopup(const QIcon &icon, const QString &title, const QString &text, const QString &actionText,
			   QWidget *parent = nullptr);
	void showToast(int milliseconds = 9000);

  signals:
	void actionClicked();

  private:
	void paintEvent(QPaintEvent *event) override;
	QTimer *m_timer = nullptr;
};
