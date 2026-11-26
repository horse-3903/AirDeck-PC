#include "appdir.hpp"
#include "debuglog.hpp"
#include "platform/windows/console.hpp"
#include "ui/mainwindow.hpp"
#include "platform/toast.hpp"
#include "platform/elevation.hpp"
#include "platform/padcleanup.hpp"
#include "settings/settings_singleton.hpp"
#include "ui/shotrunner.hpp"

#include <QLocalServer>
#include <QLocalSocket>

#include <QApplication>
#include <QDir>
#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMutex>
#include <QIcon>
#include <QPalette>
#include <QStandardPaths>
#include <QStyleFactory>
#include <memory>

#if defined(QT_DEBUG)
const QString logFilePath = "airdeck.log";
#elif defined(_WIN32)
const QString logFilePath = "airdeck.log";
#elif defined(__linux__)
const QString logFilePath =
	QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/airdeck.log";
#endif

/**
 * @brief Maximum number of log files to try to create.
 * If all are in use, logging to file will be disabled.
 */
const int8_t max_log_files = 5;

static QFile logFile(logFilePath);

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
	static QMutex logFileMutex;
	QString txt = qFormatLogMessage(type, context, msg);

	QMutexLocker locker(&logFileMutex);
	QTextStream textStream(&logFile);
	textStream << txt << Qt::endl;

	// Debug mode keeps a copy of everything in debug.log. Lines the server already wrote there are skipped.
	if (DebugLog::enabled() && !msg.startsWith(QStringLiteral("[debug]")))
		DebugLog::writeRaw(txt);

	if (type == QtFatalMsg)
		abort();
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
	// Attach to parent console if launched from terminal
	attachToParentConsole();
#endif

	// Try to find a free log file slot (airdeck.1.log, airdeck.2.log, etc.)
	static std::unique_ptr<QLockFile> lockFile;
	QFileInfo baseInfo(logFilePath);
	QString baseName = baseInfo.completeBaseName();
	QString suffix = baseInfo.suffix();
	// Use the directory from the configured path, or current dir if none specified
	QDir logDir = baseInfo.dir();

	for (int i = 1; i <= max_log_files; ++i)
	{
		QString fileName = QString("%1.%2.%3").arg(baseName).arg(i).arg(suffix);
		QString fullPath = logDir.filePath(fileName);

		auto l = std::make_unique<QLockFile>(fullPath + ".lock");
		l->setStaleLockTime(0);
		if (l->tryLock())
		{
			lockFile = std::move(l);
			logFile.setFileName(fullPath);
			break;
		}
	}

	qSetMessagePattern("[%{time yyyy-MM-ddTHH:mm:ss.zzz} %{pid} %{type}] %{message}");

	bool logFileOpened = false;
	if (lockFile)
	{
		logFileOpened = logFile.open(QIODevice::WriteOnly | QIODevice::Truncate |
									 QIODevice::Unbuffered | QIODevice::Text);
	}
	else
	{
		qWarning() << "Failed to acquire lock for any log file slot (1-" << max_log_files << "). "
		"Logging to file disabled.";
	}

	qInfo() << "Log file path:" << QFileInfo(logFile).absoluteFilePath();
	QtMessageHandler oldMessageHandler;
	if (logFileOpened)
	{
		oldMessageHandler = qInstallMessageHandler(customMessageHandler);
	}
	else
	{
		qWarning() << "Failed to open log file.";
	}

	qInfo() << "Launching app...";

	QApplication a(argc, argv);
	QApplication::setStyle(QStyleFactory::create("Fusion"));

	QPalette dark;
	dark.setColor(QPalette::Window, QColor("#000000"));
	dark.setColor(QPalette::WindowText, QColor("#E2E8F0"));
	dark.setColor(QPalette::Base, QColor("#0A0A0A"));
	dark.setColor(QPalette::AlternateBase, QColor("#111111"));
	dark.setColor(QPalette::Text, QColor("#E2E8F0"));
	dark.setColor(QPalette::Button, QColor("#1A1A1A"));
	dark.setColor(QPalette::ButtonText, QColor("#E2E8F0"));
	dark.setColor(QPalette::ToolTipBase, QColor("#111111"));
	dark.setColor(QPalette::ToolTipText, QColor("#E2E8F0"));
	dark.setColor(QPalette::Highlight, QColor("#3A3A3A"));
	dark.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
	dark.setColor(QPalette::Link, QColor("#D4D4D4"));
	dark.setColor(QPalette::PlaceholderText, QColor("#6B6B6B"));
	dark.setColor(QPalette::Disabled, QPalette::Text, QColor("#6B6B6B"));
	dark.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#6B6B6B"));
	QApplication::setPalette(dark);

	QFile themeFile(":/theme.qss");
	if (themeFile.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		a.setStyleSheet(QString::fromUtf8(themeFile.readAll()));
	}
	QApplication::setOrganizationName("horse-3903");
	QApplication::setOrganizationDomain("io.github.horse3903");
	QApplication::setApplicationName("AirDeck");
	QApplication::setWindowIcon(QIcon(":/logos/AppIcon.png"));
	QApplication::setApplicationVersion(APP_VERSION);
	qInfo() << "App data directory:" << getAppDataDir();

	// Debug mode is read once here and again whenever Preferences are saved
	DebugLog::setEnabled(SettingsSingleton::instance().qsettings()->value("server/debug", false).toBool());
	if (DebugLog::enabled())
	{
		qInfo() << "Debug mode is on. Version" << APP_VERSION << "on" << QSysInfo::prettyProductName() << QSysInfo::currentCpuArchitecture()
				<< "Qt" << qVersion() << "elevated" << isElevated() << "arguments" << QCoreApplication::arguments();
		qInfo() << "Debug log:" << DebugLog::path() << "settings file:" << SettingsSingleton::instance().qsettings()->fileName();
	}

	// Started elevated by the "Fix controller numbering" button: remove leftover virtual pads, then stop
	for (int i = 1; i < argc; ++i)
	{
		if (QString(argv[i]) == kCleanPadsArgument)
			return isElevated() ? (removeVirtualPads() >= 0 ? 0 : 1) : 1;
	}

	// Diagnostic: write the leftover virtual pads to the log and exit with how many were found
	for (int i = 1; i < argc; ++i)
	{
		if (QString(argv[i]) == "--list-pads")
		{
			const QStringList pads = findVirtualPads();
			qInfo() << "Virtual Xbox pads on the ViGEm bus:" << pads.size() << pads;
			return static_cast<int>(pads.size());
		}
	}

	// Developer tool: --ui-shots <folder> saves a PNG of every page and dialog
	QString shotsFolder;
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (QString(argv[i]) == "--ui-shots")
			shotsFolder = QString::fromLocal8Bit(argv[i + 1]);
	}

	// The Windows notification button launches us with this argument to stop the running server
	bool stopRequested = false;
	for (int i = 1; i < argc; ++i)
	{
		if (QString(argv[i]) == kStopServerArgument)
			stopRequested = true;
	}

	// Only one instance may run: a second launch just brings the first one to the front
	const QString instanceKey = "AirDeck-single-instance";
	{
		QLocalSocket probe;
		probe.connectToServer(instanceKey);
		if (shotsFolder.isEmpty() && probe.waitForConnected(500))
		{
			probe.write(stopRequested ? "stop" : "show");
			probe.waitForBytesWritten(500);
			qInfo() << "Another instance is already running, exiting.";
			return 0;
		}
	}
	if (stopRequested)
		return 0;
	// Off by default. When switched on in Preferences, AirDeck restarts itself with administrator rights.
	if (shotsFolder.isEmpty() && !isElevated() &&
		SettingsSingleton::instance().qsettings()->value("app/run_as_admin", false).toBool() && relaunchElevated())
		return 0;
	initToastIdentity();
	// A "still running" notice left by an earlier run is out of date
	clearNativeToasts();
	QObject::connect(&a, &QCoreApplication::aboutToQuit, []() { clearNativeToasts(); });
	QLocalServer::removeServer(instanceKey);
	QLocalServer instanceServer;
	instanceServer.listen(instanceKey);

	MainWindow w;
	QObject::connect(&instanceServer, &QLocalServer::newConnection, &w, [&]() {
		bool stop = false;
		while (QLocalSocket *client = instanceServer.nextPendingConnection())
		{
			client->waitForReadyRead(200);
				if (client->readAll().startsWith("stop"))
					stop = true;
				client->deleteLater();
		}
		if (stop)
				w.stopServerAndQuit();
			else
				w.showFromTray();
	});
	if (!shotsFolder.isEmpty())
	{
		// Keep the window away from the screen and never take focus from what the user is doing
		w.setAttribute(Qt::WA_ShowWithoutActivating);
		w.move(-6000, 100);
		w.show();
		auto *runner = new ShotRunner(&w, shotsFolder);
		runner->start();
	}
	else
	{
		w.show();
	}
	qInfo() << "Application initialized successfully. Version:"
			<< QApplication::applicationVersion();
	int result = QApplication::exec();
	qInfo() << "Application shutting down with exit code:" << result;

	if (logFileOpened)
	{
		qInstallMessageHandler(oldMessageHandler); // Reset message handler
		logFile.close();
		// Set permissions (allow non-admin users to read/write if run as admin)
		//! This does not modify ACLs, so the effectiveness can be limited
		// Needs more testing
		logFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
							   QFileDevice::ReadGroup | QFileDevice::WriteGroup |
							   QFileDevice::ReadUser | QFileDevice::WriteUser);
	}

	return result;
}
