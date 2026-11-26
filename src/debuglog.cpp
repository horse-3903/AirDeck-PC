#include "debuglog.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

#include <atomic>

namespace
{
constexpr qint64 kMaxBytes = 8 * 1024 * 1024;
constexpr int kMaxLineChars = 4000;
constexpr int kMaxLinesPerSecond = 1500;

std::atomic<bool> g_enabled{false};
QMutex g_mutex;
QFile g_file;
qint64 g_size = 0;

// Repeat collapsing: only the text of the previous line is kept
QString g_last;
int g_repeats = 0;

// Rate limit: a one second window
qint64 g_windowStart = 0;
int g_windowLines = 0;
int g_dropped = 0;

// Never write from inside a write
thread_local bool g_writing = false;

QString filePath()
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("debug.log"));
}

bool openFile()
{
	if (g_file.isOpen())
		return true;
	QDir().mkpath(QFileInfo(filePath()).absolutePath());
	g_file.setFileName(filePath());
	if (!g_file.open(QIODevice::Append | QIODevice::Text))
		return false;
	g_size = g_file.size();
	return true;
}

// Keeps one older file, so the log never takes more than about twice the cap
void rotateIfNeeded(qint64 incoming)
{
	if (g_size + incoming <= kMaxBytes)
		return;
	g_file.close();
	const QString old = filePath() + QStringLiteral(".1");
	QFile::remove(old);
	QFile::rename(filePath(), old);
	g_size = 0;
	openFile();
}

void append(const QString &text)
{
	if (!openFile())
		return;
	const QByteArray bytes = text.toUtf8() + '\n';
	rotateIfNeeded(bytes.size());
	if (!g_file.isOpen())
		return;
	g_file.write(bytes);
	g_file.flush();
	g_size += bytes.size();
}

QString stamp()
{
	return QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + QStringLiteral("  ");
}

// The part of a line that makes two lines "the same": everything after a leading timestamp
QString keyOf(const QString &line)
{
	const int close = line.indexOf(QStringLiteral("] "));
	if (line.startsWith(QLatin1Char('[')) && close > 0 && close < 60)
		return line.mid(close + 2);
	const int gap = line.indexOf(QStringLiteral("  "));
	if (gap > 0 && gap < 30 && line.at(0).isDigit())
		return line.mid(gap + 2);
	return line;
}

void put(QString line)
{
	if (line.size() > kMaxLineChars)
		line = line.left(kMaxLineChars) + QStringLiteral("... (cut)");

	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (now - g_windowStart >= 1000)
	{
		if (g_dropped > 0)
		{
			append(stamp() + QStringLiteral("(%1 lines left out, more than %2 per second)").arg(g_dropped).arg(kMaxLinesPerSecond));
			g_dropped = 0;
		}
		g_windowStart = now;
		g_windowLines = 0;
	}
	if (++g_windowLines > kMaxLinesPerSecond)
	{
		++g_dropped;
		return;
	}

	const QString key = keyOf(line);
	if (key == g_last)
	{
		++g_repeats;
		return;
	}
	if (g_repeats > 0)
	{
		append(stamp() + QStringLiteral("(the line above repeated %1 more times)").arg(g_repeats));
		g_repeats = 0;
	}
	g_last = key;
	append(line);
}
} // namespace

namespace DebugLog
{
void setEnabled(bool enabled)
{
	g_enabled.store(enabled);
}

bool enabled()
{
	return g_enabled.load();
}

QString path()
{
	return filePath();
}

void write(const QString &line, bool force)
{
	if (!g_enabled.load() && !force)
		return;
	if (g_writing)
		return;
	g_writing = true;
	{
		QMutexLocker lock(&g_mutex);
		put(stamp() + line);
	}
	g_writing = false;
}

void writeRaw(const QString &line)
{
	if (!g_enabled.load())
		return;
	if (g_writing)
		return;
	g_writing = true;
	{
		QMutexLocker lock(&g_mutex);
		put(line);
	}
	g_writing = false;
}
} // namespace DebugLog
