#ifndef NOTIFICATIONMANAGER_H
#define NOTIFICATIONMANAGER_H

#include <string>
#include <vector>
#include <chrono>
#include <imgui.h>

enum class NotificationType {
    Info,
    Success,
    Warning,
    Error
};

struct Notification {
    std::string title;
    std::string message;
    NotificationType type;
    std::chrono::time_point<std::chrono::steady_clock> startTime;
    float durationMs;
    float alpha = 0.0f; // For fade-in/out animation
    bool dismiss = false;
    uint64_t id;
};

class NotificationManager {
public:
    static NotificationManager& instance();

    void addNotification(const std::string& title, const std::string& message,
                        NotificationType type = NotificationType::Info,
                        float durationSeconds = 5.0f);

    void draw();

    bool hasActiveNotifications() const { return !m_notifications.empty(); }
    void clearNotifications() { m_notifications.clear(); }
    const std::vector<Notification>& getNotifications() const { return m_notifications; }

private:
    NotificationManager() = default;
    ~NotificationManager() = default;

    NotificationManager(const NotificationManager&) = delete;
    NotificationManager& operator=(const NotificationManager&) = delete;

    std::vector<Notification> m_notifications;
    uint64_t m_nextId = 0;
};

#endif // NOTIFICATIONMANAGER_H