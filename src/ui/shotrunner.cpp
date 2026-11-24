#include "shotrunner.hpp"

#include "../networking/server.hpp"
#include "../settings/settings_singleton.hpp"
#include "mainwindow.hpp"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QInputDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextStream>
#include <functional>

ShotRunner::ShotRunner(MainWindow *window, const QString &folder, QObject *parent)
	: QObject(parent), m_window(window), m_folder(folder)
{
	QDir().mkpath(m_folder);
}

void ShotRunner::save(QWidget *widget, const QString &name)
{
	if (widget == nullptr)
		return;
	widget->grab().save(m_folder + "/" + name + ".png");
	qInfo() << "Saved screenshot" << name;
}

void ShotRunner::saveWindow(const QString &name)
{
	save(m_window, name);
}

void ShotRunner::saveModal(const QString &name)
{
	save(QApplication::activeModalWidget(), name);
}

QPushButton *ShotRunner::button(const QString &text) const
{
	const auto buttons = m_window->findChildren<QPushButton *>();
	for (QPushButton *candidate : buttons)
	{
		if ((candidate->text() == text || candidate->accessibleName() == text) && candidate->isVisibleTo(m_window))
			return candidate;
	}
	return nullptr;
}

QPushButton *ShotRunner::button(const char *objectName) const
{
	return m_window->findChild<QPushButton *>(objectName);
}

void ShotRunner::click(QPushButton *target)
{
	if (target != nullptr)
		target->click();
	else
		qWarning() << "Button not found";
}

void ShotRunner::clickWithModal(QPushButton *target, const QString &name,
								const std::function<void(QWidget *)> &answer)
{
	// The click blocks inside the dialog's event loop, so the dialog is handled from a timer
	QTimer::singleShot(600, this, [this, name, answer]() {
		QWidget *modal = QApplication::activeModalWidget();
		saveModal(name);
		if (modal != nullptr)
			answer(modal);
	});
	click(target);
}

void ShotRunner::resizeWindow(int width, int height)
{
	m_window->resize(width, height);
	QApplication::processEvents();
}

void ShotRunner::schedule(const std::function<void()> &step)
{
	m_steps.push_back(step);
}

void ShotRunner::runNext()
{
	if (m_index >= m_steps.size())
	{
		m_flagTimer.setInterval(500);
		connect(&m_flagTimer, &QTimer::timeout, this, &ShotRunner::pollFlags);
		m_flagTimer.start();
		QFile done(m_folder + "/stage-a-done.txt");
		if (done.open(QIODevice::WriteOnly))
			done.write("ok");
		return;
	}
	const auto step = m_steps[m_index++];
	step();
	QTimer::singleShot(900, this, &ShotRunner::runNext);
}

void ShotRunner::pollFlags()
{
	QFile shot(m_folder + "/shot.flag");
	if (shot.exists() && shot.open(QIODevice::ReadOnly))
	{
		const QString name = QString::fromUtf8(shot.readAll()).trimmed();
		shot.close();
		QFile::remove(m_folder + "/shot.flag");
		saveWindow(name);
	}
	if (QFile::exists(m_folder + "/quit.flag"))
		QApplication::quit();
}

void ShotRunner::start()
{
	auto accept = [](QWidget *modal) {
		if (auto *box = qobject_cast<QMessageBox *>(modal))
		{
			if (QAbstractButton *yes = box->button(QMessageBox::Yes))
				yes->click();
			else if (QAbstractButton *ok = box->button(QMessageBox::Ok))
				ok->click();
			else
				box->accept();
		}
		else
		{
			modal->close();
		}
	};
	auto dismiss = [](QWidget *modal) { modal->close(); };

	schedule([this] { resizeWindow(960, 540); saveWindow("01-home"); });

	// Preferences, General tab
	schedule([this] { click(button("settingsButton")); });
	schedule([this] { saveWindow("02-preferences-general"); });
	schedule([this] { resizeWindow(960, 900); });
	schedule([this] { saveWindow("03-preferences-general-tall"); });
	const char *radios[] = {"vigemExecutorRadio", "gamepadExecutorRadio",
							"keyboardMouseExecutorRadio"};
	const char *radioNames[] = {"04-preferences-mode-vigem", "05-preferences-mode-inputinjector",
								"06-preferences-mode-keyboard"};
	for (int i = 0; i < 3; ++i)
	{
		const QString radio = radios[i];
		const QString name = radioNames[i];
		schedule([this, radio, name] {
			if (auto *r = m_window->findChild<QRadioButton *>(radio))
				r->setChecked(true);
			QApplication::processEvents();
			saveWindow(name);
		});
	}

	// Preferences, Keymap tab and its inner tabs
	schedule([this] {
		if (auto *sidebar = m_window->findChild<QListWidget *>("prefsSidebar"))
			sidebar->setCurrentRow(1);
	});
	schedule([this] {
		auto *tabs = m_window->findChild<QTabWidget *>("keymapTabWidget");
		saveWindow("07-preferences-keymap");
		if (tabs != nullptr)
			tabs->setCurrentIndex(0);
	});
	for (int i = 0; i < 4; ++i)
	{
		schedule([this, i] {
			auto *tabs = m_window->findChild<QTabWidget *>("keymapTabWidget");
			if (tabs == nullptr || i >= tabs->count())
				return;
			tabs->setCurrentIndex(i);
			QApplication::processEvents();
			saveWindow(QString("08-preferences-keymap-%1-%2")
						   .arg(i + 1)
						   .arg(tabs->tabText(i).replace('/', '-').replace(' ', '-')));
		});
	}

	// A dialog that opens right after another one closes: capture it, then answer it
	auto followUp = [this, accept](const QString &name) {
		QTimer::singleShot(600, this, [this, name, accept]() {
			QWidget *modal = QApplication::activeModalWidget();
			saveModal(name);
			if (modal != nullptr)
				accept(modal);
		});
	};

	// Profile dialogs
	schedule([this, dismiss] {
		clickWithModal(button("deleteProfileButton"), "09-modal-cannot-delete-default", dismiss);
	});
	schedule([this, followUp] {
		clickWithModal(button("newProfileButton"), "10-modal-new-profile",
					   [followUp](QWidget *modal) {
						   if (auto *input = qobject_cast<QInputDialog *>(modal))
						   {
							   input->setTextValue("Gaming");
							   followUp("11-modal-profile-created");
							   input->accept();
						   }
					   });
	});
	schedule([this, accept, followUp] {
		clickWithModal(button("deleteProfileButton"), "12-modal-delete-profile",
					   [accept, followUp](QWidget *modal) {
						   followUp("13-modal-profile-deleted");
						   accept(modal);
					   });
	});
	schedule([this, accept, followUp] {
		clickWithModal(button(QStringLiteral("Restore Defaults")), "14-modal-restore-defaults",
					   [accept, followUp](QWidget *modal) {
						   followUp("15-modal-defaults-restored");
						   accept(modal);
					   });
	});

	// Back out of preferences
	schedule([this] { click(button(QStringLiteral("Cancel"))); });
	schedule([this] { resizeWindow(960, 540); saveWindow("16-home-after-preferences"); });

	// About
	schedule([this] { click(button("pushButton_About")); });
	schedule([this] { saveWindow("17-about"); });
	schedule([this] { click(button("backButton")); });

	// Server, ViGEm mode (default), no phones yet
	schedule([this] { SettingsSingleton::instance().setExecutorType(ExecutorType::VigemExecutor); });
	schedule([this] { click(button("startButton")); });
	schedule([this] { saveWindow("18-server-vigem-waiting"); });
	schedule([this] { resizeWindow(960, 760); });
	schedule([this] {
		saveWindow("19-server-vigem-waiting-tall");
		resizeWindow(960, 540);
	});
	schedule([this] { click(button(QStringLiteral("Back to Home"))); });
	schedule([this] { saveWindow("20-home-server-running"); });
	schedule([this] { click(button("startButton")); });
	schedule([this] { click(button("stopButton")); });
	schedule([this] { saveWindow("21-home-after-stop"); });

	// Server in keyboard mode, waiting for the emulator to connect
	schedule([this] { SettingsSingleton::instance().setExecutorType(ExecutorType::KeyboardMouseExecutor); });
	schedule([this] { click(button("startButton")); });
	schedule([this] {
		saveWindow("22-server-keyboard-waiting");
		if (auto *server = m_window->findChild<Server *>())
		{
			QFile port(m_folder + "/port.txt");
			if (port.open(QIODevice::WriteOnly))
				port.write(QByteArray::number(server->tcpServer->serverPort()));
		}
	});
	// With a test client connected from outside, shoot the Controllers tab and the tester
	schedule([this] {
		QEventLoop wait;
		QTimer::singleShot(6000, &wait, &QEventLoop::quit);
		wait.exec();
		if (auto *server = m_window->findChild<Server *>())
		{
			const auto tabs = server->findChildren<QPushButton *>();
			for (QPushButton *tab : tabs)
				if (tab->text() == QStringLiteral("Controllers"))
					tab->click();
		}
	});
	schedule([this] { saveWindow("23-controllers-connected"); });
	schedule([this] { click(button(QStringLiteral("Input test"))); });
	schedule([this] { saveWindow("24-tester"); });

	QTimer::singleShot(500, this, &ShotRunner::runNext);
}
