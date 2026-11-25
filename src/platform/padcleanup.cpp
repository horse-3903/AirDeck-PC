#include "padcleanup.hpp"

#ifdef _WIN32

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#include <windows.h>
#include <cfgmgr32.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace
{
struct PadNode
{
	DEVINST node = 0;
	QString id;
};

// Virtual pads are the Xbox 360 USB devices whose parent is the ViGEm bus (ROOT\SYSTEM\...). A real wired
// Xbox 360 pad has the same hardware ID but hangs off a USB hub, so it is left alone.
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
		if (!text.startsWith(QStringLiteral("USB\\VID_045E&PID_028E\\"), Qt::CaseInsensitive))
			continue;
		DEVINST node = 0, parent = 0;
		if (CM_Locate_DevNodeW(&node, const_cast<DEVINSTID_W>(id), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
			continue;
		if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS)
			continue;
		wchar_t parentId[MAX_DEVICE_ID_LEN] = {};
		if (CM_Get_Device_IDW(parent, parentId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
			continue;
		if (QString::fromWCharArray(parentId).startsWith(QStringLiteral("ROOT\\SYSTEM"), Qt::CaseInsensitive))
			found.push_back({node, text});
	}
	return found;
}
} // namespace

QStringList findVirtualPads()
{
	QStringList ids;
	for (const PadNode &pad : virtualPadNodes())
		ids << pad.id;
	return ids;
}

int removeVirtualPads()
{
	int removed = 0;
	for (const PadNode &pad : virtualPadNodes())
	{
		PNP_VETO_TYPE veto = PNP_VetoTypeUnknown;
		wchar_t vetoName[MAX_PATH] = {};
		CONFIGRET result = CM_Query_And_Remove_SubTreeW(pad.node, &veto, vetoName, MAX_PATH, CM_REMOVE_NO_RESTART);
		if (result != CR_SUCCESS)
			result = CM_Uninstall_DevNode(pad.node, 0);
		if (result == CR_SUCCESS)
		{
			++removed;
			qInfo() << "Removed leftover virtual pad" << pad.id;
		}
		else
		{
			qWarning() << "Could not remove leftover virtual pad" << pad.id << "(code" << result << ", vetoed by"
					   << QString::fromWCharArray(vetoName) << ")";
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
			WaitForSingleObject(info.hProcess, 20000);
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
