#include "toast.hpp"

#ifdef _WIN32

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#include <windows.h>
#include <shobjidl.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>

namespace
{
constexpr const wchar_t *kAppId = L"AirDeck.App";

void setRegString(const std::wstring &key, const wchar_t *name, const std::wstring &value)
{
	RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
					static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}
} // namespace

void initToastIdentity()
{
	SetCurrentProcessExplicitAppUserModelID(kAppId);

	const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
	setRegString(L"Software\\Classes\\AppUserModelId\\AirDeck.App", L"DisplayName", L"AirDeck");
	setRegString(L"Software\\Classes\\AppUserModelId\\AirDeck.App", L"IconBackgroundColor", L"FF202020");

	setRegString(L"Software\\Classes\\airdeck-stop", nullptr, L"URL:AirDeck");
	setRegString(L"Software\\Classes\\airdeck-stop", L"URL Protocol", L"");
	setRegString(L"Software\\Classes\\airdeck-stop\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"");
}

bool showNativeToast(const QString &title, const QString &text, const QString &actionLabel)
{
	try
	{
		using namespace winrt::Windows::UI::Notifications;
		using winrt::Windows::Data::Xml::Dom::XmlDocument;

		const auto escape = [](const QString &s) { return s.toHtmlEscaped().toStdWString(); };
		std::wstring xml = L"<toast scenario=\"default\"><visual><binding template=\"ToastGeneric\"><text>" +
						   escape(title) + L"</text><text>" + escape(text) +
						   L"</text></binding></visual><actions><action content=\"" + escape(actionLabel) +
						   L"\" arguments=\"" + QString::fromLatin1(kStopServerArgument).toStdWString() +
						   L"\" activationType=\"protocol\"/></actions></toast>";
		XmlDocument doc;
		doc.LoadXml(xml);
		ToastNotifier notifier = ToastNotificationManager::CreateToastNotifier(kAppId);
		notifier.Show(ToastNotification(doc));
		return true;
	}
	catch (const winrt::hresult_error &error)
	{
		qWarning() << "Windows toast failed:" << QString::fromStdWString(std::wstring(error.message()));
		return false;
	}
	catch (...)
	{
		return false;
	}
}

#else

void initToastIdentity() {}
bool showNativeToast(const QString &, const QString &, const QString &) { return false; }

#endif
