#include "appdir.hpp"
#include "platform/windows/console.hpp"
#include "ui/mainwindow.hpp"
#include "platform/toast.hpp"
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
const QString logFilePath = "virtualgamepad.log";
#elif defined(_WIN32)
const QString logFilePath = "virtualgamepad.log";
#elif defined(__linux__)
const QString logFilePath =
	QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/virtualgamepad.log";
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

	if (type == QtFatalMsg)
		abort();
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
	// Attach to parent console if launched from terminal
	attachToParentConsole();
#endif

	// Try to find a free log file slot (virtualgamepad.1.log, virtualgamepad.2.log, etc.)
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
	QApplication::setOrganizationName("kitswas");
	QApplication::setOrganizationDomain("io.github.kitswas");
	QApplication::setApplicationName("VirtualGamePad");
	QApplication::setWindowIcon(QIcon(":/logos/AppIcon.png"));
	QApplication::setApplicationVersion(APP_VERSION);
	qInfo() << "App data directory:" << getAppDataDir();

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
	const QString instanceKey = "VirtualGamePadPC-single-instance";
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
	initToastIdentity();
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
