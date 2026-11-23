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
	void showFromTray();

  protected:
	void closeEvent(QCloseEvent *event) override;

  private:
	Ui::MainWindow *ui;
	QSystemTrayIcon *trayIcon = nullptr;
	bool quitRequested = false;
	bool trayHintShown = false;
};
