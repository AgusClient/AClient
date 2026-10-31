#ifndef ENGINE_CLIENT_NOTIFICATIONS_H
#define ENGINE_CLIENT_NOTIFICATIONS_H

#include <base/detect.h>
#include <engine/notifications.h>

class CNotifications : public INotifications
{
#if defined(CONF_FAMILY_WINDOWS)
	void *m_hWnd = nullptr;
	bool m_TrayAdded = false;
#endif

public:
	void Init(const char *pAppname) override;
	void Shutdown() override;
	void Notify(const char *pTitle, const char *pMessage) override;
};

#endif // ENGINE_CLIENT_NOTIFICATIONS_H
