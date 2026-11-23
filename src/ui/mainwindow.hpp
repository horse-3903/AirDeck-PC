#pragma once

#include <QMainWindow>

class QSystemTrayIcon;

namespace Ui
{
class MainWindow;
}

class MainWindow final : public QMainWindow
{
	Q_OBJECT

  public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow() override;

  protected:
	void closeEvent(QCloseEvent *event) override;

  private:
	void showFromTray();

	Ui::MainWindow *ui;
	QSystemTrayIcon *trayIcon = nullptr;
	bool quitRequested = false;
	bool trayHintShown = false;
};
