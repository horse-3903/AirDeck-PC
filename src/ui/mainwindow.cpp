#include "mainwindow.hpp"

#include "../settings/settings_singleton.hpp"
#include "../platform/elevation.hpp"
#include "mainmenu.hpp"
#include "toastpopup.hpp"
#include "ui_mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QMenu>
#include <QMetaObject>
#include <QSystemTrayIcon>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
	qDebug() << "Initializing MainWindow";

	SettingsSingleton::instance().loadAll();
	ui->setupUi(this);
	ui->statusbar->hide();
	resize(1200, 760);
	if (isElevated())
		setWindowTitle(windowTitle() + tr(" (Administrator)"));
	ui->stackedWidget->addWidget(new MainMenu(ui->stackedWidget));

	if (QSystemTrayIcon::isSystemTrayAvailable())
	{
		QApplication::setQuitOnLastWindowClosed(false);
		trayIcon = new QSystemTrayIcon(windowIcon(), this);
		trayIcon->setToolTip(tr("Virtual Gamepad"));
		auto *menu = new QMenu(this);
		menu->addAction(tr("Show"), this, [this]() { showFromTray(); });
		menu->addAction(tr("Stop server"), this, [this]() { stopServerAndQuit(); });
		menu->addAction(tr("Quit"), this, [this]() {
			quitRequested = true;
			QApplication::quit();
		});
		trayIcon->setContextMenu(menu);
		connect(trayIcon, &QSystemTrayIcon::activated, this,
				[this](QSystemTrayIcon::ActivationReason reason) {
					if (reason == QSystemTrayIcon::Trigger ||
						reason == QSystemTrayIcon::DoubleClick)
						showFromTray();
				});
	}

	qDebug() << "MainWindow initialized successfully";
}

MainWindow::~MainWindow()
{
	qDebug() << "Destroying MainWindow";
	delete ui;
}

void MainWindow::stopServerAndQuit()
{
	if (QWidget *server = findChild<QWidget *>("Server"))
		QMetaObject::invokeMethod(server, "destroyServer", Qt::DirectConnection);
	quitRequested = true;
	QApplication::quit();
}

void MainWindow::showFromTray()
{
	showNormal();
	raise();
	activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	const bool serverRunning = findChild<QWidget *>("Server") != nullptr;
	if (trayIcon != nullptr && serverRunning && !quitRequested)
	{
		hide();
		trayIcon->show();
		if (!trayHintShown)
		{
			trayHintShown = true;
			auto *toast = new ToastPopup(windowIcon(), tr("Virtual Gamepad is still running"),
										 tr("The server keeps running in the background. Open the tray icon to reopen the window."),
										 tr("Stop server"));
			connect(toast, &ToastPopup::actionClicked, this, [this]() { stopServerAndQuit(); });
			toast->showToast();
		}
		event->ignore();
		return;
	}
	QApplication::quit();
	event->accept();
}
