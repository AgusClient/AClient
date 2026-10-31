#include "notifications.h"

#include <base/detect.h>

#if defined(CONF_PLATFORM_MACOS)
// Code is in src/macos/notification.mm.
void NotificationsNotifyMacOsInternal(const char *pTitle, const char *pMessage);
#elif defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#include <shellapi.h>
#elif defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_HAIKU) && !defined(CONF_PLATFORM_EMSCRIPTEN) && !defined(CONF_PLATFORM_IOS)
#include <libnotify/notify.h>
#define NOTIFICATIONS_USE_LIBNOTIFY
#endif

void CNotifications::Init(const char *pAppname)
{
#if defined(NOTIFICATIONS_USE_LIBNOTIFY)
	notify_init(pAppname);
#elif defined(CONF_FAMILY_WINDOWS)
	WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"AClientNotifWindow";
	RegisterClassExW(&wc);
	m_hWnd = (void *)CreateWindowExW(
		WS_EX_TOOLWINDOW,
		L"AClientNotifWindow",
		L"AClient Notifications",
		WS_POPUP,
		0, 0, 0, 0,
		NULL, NULL, wc.hInstance, NULL);
#endif
}

void CNotifications::Shutdown()
{
#if defined(NOTIFICATIONS_USE_LIBNOTIFY)
	notify_uninit();
#elif defined(CONF_FAMILY_WINDOWS)
	if(m_hWnd)
	{
		HWND hWnd = (HWND)m_hWnd;
		if(m_TrayAdded)
		{
			NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
			nid.hWnd = hWnd;
			nid.uID = 1;
			Shell_NotifyIconW(NIM_DELETE, &nid);
			m_TrayAdded = false;
		}
		DestroyWindow(hWnd);
		m_hWnd = nullptr;
		UnregisterClassW(L"AClientNotifWindow", GetModuleHandleW(NULL));
	}
#endif
}

void CNotifications::Notify(const char *pTitle, const char *pMessage)
{
#if defined(CONF_PLATFORM_MACOS)
	NotificationsNotifyMacOsInternal(pTitle, pMessage);
#elif defined(NOTIFICATIONS_USE_LIBNOTIFY)
	NotifyNotification *pNotif = notify_notification_new(pTitle, pMessage, "ddnet");
	if(pNotif)
	{
		notify_notification_show(pNotif, nullptr);
		g_object_unref(G_OBJECT(pNotif));
	}
#elif defined(CONF_FAMILY_WINDOWS)
	if(!m_hWnd)
		return;

	HWND hWnd = (HWND)m_hWnd;
	NOTIFYICONDATAW nid = {sizeof(NOTIFYICONDATAW)};
	nid.hWnd = hWnd;
	nid.uID = 1;
	nid.uFlags = NIF_INFO | NIF_ICON | NIF_TIP;

	HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
	if(!hIcon)
		hIcon = LoadIconW(NULL, IDI_APPLICATION);
	nid.hIcon = hIcon;

	wcsncpy_s(nid.szTip, L"AClient", _TRUNCATE);

	MultiByteToWideChar(CP_UTF8, 0, pTitle ? pTitle : "", -1, nid.szInfoTitle, (int)(sizeof(nid.szInfoTitle) / sizeof(nid.szInfoTitle[0])));
	MultiByteToWideChar(CP_UTF8, 0, pMessage ? pMessage : "", -1, nid.szInfo, (int)(sizeof(nid.szInfo) / sizeof(nid.szInfo[0])));
	nid.dwInfoFlags = NIIF_INFO;

	if(!m_TrayAdded)
	{
		if(Shell_NotifyIconW(NIM_ADD, &nid))
		{
			m_TrayAdded = true;
			nid.uVersion = NOTIFYICON_VERSION_4;
			Shell_NotifyIconW(NIM_SETVERSION, &nid);
		}
	}
	else
	{
		Shell_NotifyIconW(NIM_MODIFY, &nid);
	}
#endif
}

INotifications *CreateNotifications()
{
	return new CNotifications();
}
