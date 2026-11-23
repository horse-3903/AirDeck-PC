#include "about.hpp"

#include "badge.hpp"
#include "ui_about.h"

About::About(QStackedWidget *parent) : QWidget(parent), ui(new Ui::About)
{
	qDebug() << "Initializing About dialog";

	ui->setupUi(this);
	ui->versionLabel->setText(tr("Version: %1").arg(QApplication::applicationVersion()));

	// Setup text description
	QString aboutText = tr("<p>This is a part of the <a "
						   "href='https://kitswas.github.io/VirtualGamePad/'>Virtual Gamepad "
						   "project</a>.</p>"
						   "<p>The source code is available on <a "
						   "href='https://github.com/Kitswas/VirtualGamePad-PC'>GitHub</a>.</p>"
						   "Report bugs or request features at <a "
						   "href='https://github.com/kitswas/VirtualGamePad-PC/issues/new/"
						   "choose'>GitHub Issues</a>.</p>");

	ui->descriptionBox->setHtml("<div align='center'>" + aboutText + "</div>");
	ui->descriptionBox->setMaximumWidth(620);
	ui->descriptionBox->setFixedHeight(190);
	ui->descriptionBox->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	ui->verticalLayout->setAlignment(ui->descriptionBox, Qt::AlignHCenter);
	ui->backButton->setMaximumWidth(180);
	ui->verticalLayout->setAlignment(ui->backButton, Qt::AlignHCenter);
	ui->descriptionBox->setOpenExternalLinks(true);
	ui->descriptionBox->setReadOnly(true);

	// GitHub URLs
	const QString repoUrl = "https://github.com/kitswas/VirtualGamePad/";
	const QString serverLicenseUrl =
		"https://github.com/kitswas/VirtualGamePad-PC/blob/main/LICENCE.TXT";
	const QString releaseUrl = "https://github.com/kitswas/VirtualGamePad-PC/releases/latest";

	qDebug() << "Loading GitHub badges for project information";

	// Version and license are plain text so the page looks the same offline
	for (auto *badge : {static_cast<QWidget *>(ui->licenseBadge), static_cast<QWidget *>(ui->starsBadge),
				static_cast<QWidget *>(ui->downloadsBadge), static_cast<QWidget *>(ui->releaseBadge)})
		badge->hide();
	ui->versionLabel->setText(tr("Version %1   |   GPL-3.0 license").arg(QApplication::applicationVersion()));
	
	qDebug() << "About dialog initialized successfully";
}

About::~About()
{
	delete ui;
}

void About::on_backButton_clicked()
{
	qDebug() << "Closing About dialog";
	emit navigateBack();
	this->deleteLater();
}
