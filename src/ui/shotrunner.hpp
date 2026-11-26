#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <vector>

class MainWindow;
class QPushButton;
class QWidget;

/**
 * Developer tool: walks through every page and dialog of the app and saves a PNG of each,
 * without needing any mouse input. Started with `--ui-shots <folder>`.
 */
class ShotRunner : public QObject
{
	Q_OBJECT

  public:
	ShotRunner(MainWindow *window, const QString &folder, QObject *parent = nullptr);
	void start();

  private:
	void save(QWidget *widget, const QString &name);
	void saveWindow(const QString &name);
	void saveModal(const QString &name);
	QPushButton *button(const QString &text) const;
	QPushButton *button(const char *objectName) const;
	void click(QPushButton *target);
	void clickWithModal(QPushButton *target, const QString &name,
						const std::function<void(QWidget *)> &answer);
	void resizeWindow(int width, int height);
	void schedule(const std::function<void()> &step);
	void runNext();
	void pollFlags();

	MainWindow *m_window;
	QString m_folder;
	std::vector<std::function<void()>> m_steps;
	size_t m_index = 0;
	QTimer m_flagTimer;
};
