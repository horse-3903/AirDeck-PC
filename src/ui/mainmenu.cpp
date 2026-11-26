#include "mainmenu.hpp"

#include "../networking/server.hpp"
#include "about.hpp"
#include "preferences.hpp"
#include "ui_mainmenu.h"

#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>

MainMenu::MainMenu(QStackedWidget *parent) : QWidget(parent), ui(new Ui::MainMenu), stack(parent)
{
	ui->setupUi(this);

	// The logo is an SVG, drawn at the label height so it stays sharp on any display scale
	{
		QSvgRenderer renderer(QStringLiteral(":/logos/HeroController.svg"));
		const qreal dpr = devicePixelRatioF();
		const QSize logical(static_cast<int>(170 * 260.0 / 166.0), 170);
		QPixmap logo(logical * dpr);
		logo.setDevicePixelRatio(dpr);
		logo.fill(Qt::transparent);
		QPainter painter(&logo);
		renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(logical)));
		painter.end();
		ui->logoLabel->setPixmap(logo);
	}
	ui->startButton->setMinimumWidth(340);
	ui->startButton->setMaximumWidth(420);
	auto *serverStatus = new QLabel(tr("Server stopped"), this);
	serverStatus->setObjectName("serverStatus");
	serverStatus->setProperty("role", "caption");
	serverStatus->setAlignment(Qt::AlignCenter);
	ui->verticalLayout->insertWidget(ui->verticalLayout->indexOf(ui->subtitleLabel) + 1, serverStatus);
	ui->settingsButton->setMinimumWidth(164);
	ui->pushButton_About->setMinimumWidth(164);
	ui->settingsButton->setMaximumWidth(204);
	ui->pushButton_About->setMaximumWidth(204);
	ui->verticalLayout->setAlignment(ui->startButton, Qt::AlignHCenter);
	ui->verticalLayout->setAlignment(ui->secondaryLayout, Qt::AlignHCenter);
	for (QPushButton *button : {ui->startButton, ui->settingsButton, ui->pushButton_About})
	{
		button->setAutoDefault(false);
		button->setFocusPolicy(Qt::NoFocus);
	}
	connect(ui->settingsButton, &QPushButton::clicked, this, &MainMenu::launch_preferences);
	connect(ui->startButton, &QPushButton::clicked, this, &MainMenu::launch_server);
	connect(ui->pushButton_About, &QPushButton::clicked, this, &MainMenu::launch_about);
}

MainMenu::~MainMenu()
{
	delete ui;
}

void MainMenu::launch_server()
{
	if (activeServer)
	{
		stack->setCurrentWidget(activeServer);
		return;
	}
	qInfo() << "Launching server from main menu";

	try
	{
		auto *server = new Server(stack);
		activeServer = server;
		stack->addWidget(server);
		stack->setCurrentWidget(server);
		ui->startButton->setText(tr("Back to Server"));
		// The server keeps running while the user visits the home screen
		connect(server, &Server::homeRequested, this, [this]() { stack->setCurrentWidget(this); });
		// When the server is stopped, switch back to main menu
		connect(server, &Server::navigateBack, this, [this]() {
			stack->setCurrentWidget(this);
		});
		connect(server, &Server::statusChanged, this, [this](const QString &text) {
			if (auto *label = findChild<QLabel *>("serverStatus"))
				label->setText(text);
		});
		connect(server, &QObject::destroyed, this, [this]() {
			ui->startButton->setText(tr("Start Server"));
			if (auto *label = findChild<QLabel *>("serverStatus"))
				label->setText(tr("Server stopped"));
		});
	}
	catch (const std::exception &e)
	{
		qCritical() << "Failed to launch server:" << e.what();
		QMessageBox::critical(this, tr("Error"), tr("Failed to launch server: %1").arg(e.what()));
	}
}

void MainMenu::launch_preferences()
{
	qInfo() << "Opening preferences dialog";

	auto *preferences = new Preferences();
	stack->addWidget(preferences);
	stack->setCurrentWidget(preferences);
	// When preferences is closed, switch back to main menu
	// A running server restarts so the saved settings take effect
	connect(preferences, &Preferences::saved, this, [this]() {
		if (activeServer)
			QMetaObject::invokeMethod(activeServer, "applySettings", Qt::QueuedConnection);
	});
	connect(preferences, &Preferences::navigateBack, this, [this]() {
		stack->setCurrentWidget(this);
	});
}

void MainMenu::launch_about()
{
	qDebug() << "Opening about dialog";

	auto *about = new About();
	stack->addWidget(about);
	stack->setCurrentWidget(about);
	// When about is closed, switch back to main menu
	connect(about, &About::navigateBack, this, [this]() {
		stack->setCurrentWidget(this);
	});
}
