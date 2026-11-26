#include "padcleanup.hpp"

#ifdef _WIN32

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>

#include <windows.h>
#include <cfgmgr32.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
struct PadNode
{
	DEVINST node = 0;
	QString id;
	bool child = false;
	bool orphan = false;
};

// The "parent" property of a device, readable even when that parent is no longer plugged in
const DEVPROPKEY kParentKey = {{0x4340a6c5, 0x93fa, 0x4706, {0x97, 0x2c, 0x7b, 0x64, 0x80, 0x08, 0xa5, 0xa7}}, 8};

QString parentIdOf(DEVINST node)
{
	DEVPROPTYPE type = 0;
	wchar_t buffer[MAX_DEVICE_ID_LEN] = {};
	ULONG size = sizeof(buffer);
	if (CM_Get_DevNode_PropertyW(node, &kParentKey, &type, reinterpret_cast<PBYTE>(buffer), &size, 0) != CR_SUCCESS)
		return {};
	return QString::fromWCharArray(buffer);
}

// ViGEm numbers its pads 01, 02, ...; a real wired Xbox 360 pad has a long hub-assigned instance ID instead
bool isVirtualInstance(const QString &id)
{
	static const QRegularExpression pattern(QStringLiteral(R"(^USB\\VID_045E&PID_028E\\\d+$)"),
											QRegularExpression::CaseInsensitiveOption);
	return pattern.match(id).hasMatch();
}

// Virtual Xbox 360 pads and the controller entries hanging off them. A pad removed the plain way can leave
// those entries behind, still listed in Windows' game controllers, so they count and get removed too.
std::vector<PadNode> virtualPadNodes()
{
	std::vector<PadNode> found;
	ULONG length = 0;
	const ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
	if (CM_Get_Device_ID_List_SizeW(&length, L"USB", flags) != CR_SUCCESS || length < 2)
		return found;
	std::vector<wchar_t> buffer(length);
	if (CM_Get_Device_ID_ListW(L"USB", buffer.data(), length, flags) != CR_SUCCESS)
		return found;
	for (const wchar_t *id = buffer.data(); *id != L'\0'; id += wcslen(id) + 1)
	{
		const QString text = QString::fromWCharArray(id);
		const bool isPad = text.startsWith(QStringLiteral("USB\\VID_045E&PID_028E\\"), Qt::CaseInsensitive);
		const bool isChild = text.startsWith(QStringLiteral("USB\\VID_045E&PID_028E&IG_"), Qt::CaseInsensitive);
		if (!isPad && !isChild)
			continue;
		DEVINST node = 0, parent = 0;
		if (CM_Locate_DevNodeW(&node, const_cast<DEVINSTID_W>(id), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
			continue;
		if (isPad)
		{
			// A real wired Xbox 360 pad has the same hardware ID but hangs off a USB hub, so it is left alone
			wchar_t parentId[MAX_DEVICE_ID_LEN] = {};
			if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS ||
				CM_Get_Device_IDW(parent, parentId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
				continue;
			if (QString::fromWCharArray(parentId).startsWith(QStringLiteral("ROOT\\SYSTEM"), Qt::CaseInsensitive))
				found.push_back({node, text, false, false});
		}
		else
		{
			const QString parentId = parentIdOf(node);
			if (!isVirtualInstance(parentId))
				continue;
			DEVINST present = 0;
			const bool parentPresent =
				CM_Locate_DevNodeW(&present, const_cast<DEVINSTID_W>(reinterpret_cast<const wchar_t *>(parentId.utf16())),
								   CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS;
			found.push_back({node, text, true, !parentPresent});
		}
	}
	return found;
}
} // namespace

QStringList findVirtualPads()
{
	QStringList ids;
	for (const PadNode &pad : virtualPadNodes())
	{
		// Entries of a live pad belong to it; only the pad itself or a stranded entry is something left over
		if (!pad.child || pad.orphan)
			ids << pad.id;
	}
	return ids;
}

int removeVirtualPads()
{
	auto nodes = virtualPadNodes();
	int removed = 0;
	// Entries first, then the pads they hang off. Each group goes in parallel, since every removal takes a second or so.
	for (const bool children : {true, false})
	{
		std::vector<std::pair<QProcess *, QString>> running;
		for (const PadNode &pad : nodes)
		{
			if (pad.child != children)
				continue;
			auto *process = new QProcess();
			process->start(QStringLiteral("pnputil.exe"), {QStringLiteral("/remove-device"), pad.id, QStringLiteral("/subtree")});
			running.emplace_back(process, pad.id);
		}
		for (auto &entry : running)
		{
			entry.first->waitForFinished(60000);
			if (entry.first->exitStatus() == QProcess::NormalExit && entry.first->exitCode() == 0)
			{
				++removed;
				qInfo() << "Removed virtual pad entry" << entry.second;
			}
			else
			{
				qWarning() << "Could not remove virtual pad entry" << entry.second;
			}
			delete entry.first;
		}
	}
	return removed;
}

static bool runElevated(const QString &arguments, bool wait, DWORD *exitCode)
{
	const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
	const std::wstring parameters = arguments.toStdWString();
	SHELLEXECUTEINFOW info{};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS;
	info.lpVerb = L"runas";
	info.lpFile = exe.c_str();
	info.lpParameters = parameters.c_str();
	info.nShow = wait ? SW_HIDE : SW_SHOWNORMAL;
	if (!ShellExecuteExW(&info))
		return false;
	if (info.hProcess != nullptr)
	{
		if (wait)
		{
			WaitForSingleObject(info.hProcess, 90000);
			DWORD code = 1;
			GetExitCodeProcess(info.hProcess, &code);
			if (exitCode != nullptr)
				*exitCode = code;
		}
		CloseHandle(info.hProcess);
	}
	return true;
}

bool removeVirtualPadsElevated()
{
	DWORD code = 1;
	return runElevated(QString::fromLatin1(kCleanPadsArgument), true, &code) && code == 0;
}

bool relaunchElevated()
{
	QStringList quoted;
	const QStringList arguments = QCoreApplication::arguments();
	for (int i = 1; i < arguments.size(); ++i)
		quoted << QStringLiteral("\"%1\"").arg(arguments.at(i));
	return runElevated(quoted.join(QLatin1Char(' ')), false, nullptr);
}

#else

QStringList findVirtualPads() { return {}; }
int removeVirtualPads() { return 0; }
bool removeVirtualPadsElevated() { return false; }
bool relaunchElevated() { return false; }

#endif
