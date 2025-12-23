#include "NotificationManager.h"
#include <algorithm>
#include <cstdio>

NotificationManager& NotificationManager::instance() {
    static NotificationManager instance;
    return instance;
}

void NotificationManager::addNotification(const std::string& title, const std::string& message, NotificationType type, float durationSeconds) {
    Notification n;
    n.title = title;
    n.message = message;
    n.type = type;
    n.startTime = std::chrono::steady_clock::now();
    n.durationMs = durationSeconds * 1000.0f;
    n.id = m_nextId++;
    m_notifications.push_back(n);
}

static ImVec4 GetNotificationColor(NotificationType type, bool isDarkTheme) {
    if (isDarkTheme) {
        switch (type) {
            case NotificationType::Success: return {0.2f, 0.8f, 0.2f, 1.0f};
            case NotificationType::Warning: return {1.0f, 0.8f, 0.0f, 1.0f};
            case NotificationType::Error:   return {1.0f, 0.4f, 0.4f, 1.0f};
            case NotificationType::Info:
            default:                        return {0.4f, 0.7f, 1.0f, 1.0f};
        }
    }
    switch (type) {
        case NotificationType::Success: return {0.0f, 0.6f, 0.1f, 1.0f};
        case NotificationType::Warning: return {0.9f, 0.5f, 0.0f, 1.0f};
        case NotificationType::Error:   return {0.8f, 0.1f, 0.1f, 1.0f};
        case NotificationType::Info:
        default:                        return {0.0f, 0.4f, 0.8f, 1.0f};
    }
}

void NotificationManager::draw() {
    if (m_notifications.empty()) return;

    const float PADDING = 16.0f;
    const float NOTIFICATION_WIDTH = 360.0f;
    const float ROUNDING = 6.0f;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 workPos = viewport->WorkPos;
    ImVec2 workSize = viewport->WorkSize;
    ImVec2 nextWindowPos = ImVec2(workPos.x + workSize.x - PADDING, workPos.y + workSize.y - PADDING);

    auto currentTime = std::chrono::steady_clock::now();

    ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    float luminance = bg.x * 0.299f + bg.y * 0.587f + bg.z * 0.114f;
    bool isDarkTheme = luminance < 0.5f;

    char windowIdBuffer[32];

    for (auto it = m_notifications.rbegin(); it != m_notifications.rend(); ++it) {
        Notification& n = *it;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - n.startTime).count();
        float lifeParams = (float)elapsed / n.durationMs;

        if (elapsed > n.durationMs) {
            n.dismiss = true;
            continue;
        }

        if (lifeParams < 0.1f) n.alpha = lifeParams / 0.1f;
        else if (lifeParams > 0.8f) n.alpha = 1.0f - ((lifeParams - 0.8f) / 0.2f);
        else n.alpha = 1.0f;

        ImVec4 winBgColor = bg;
        winBgColor.w = 0.9f;

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, n.alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ROUNDING);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, winBgColor);

        snprintf(windowIdBuffer, sizeof(windowIdBuffer), "##Notify%lu", n.id);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoDocking;

        ImGui::SetNextWindowPos(nextWindowPos, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(NOTIFICATION_WIDTH, 0), ImVec2(NOTIFICATION_WIDTH, FLT_MAX));

        if (ImGui::Begin(windowIdBuffer, nullptr, flags)) {
            ImVec4 typeColor = GetNotificationColor(n.type, isDarkTheme);

            float closeBtnSize = ImGui::GetTextLineHeight();
            ImGui::PushStyleColor(ImGuiCol_Text, typeColor);
            ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x - closeBtnSize - 5.0f);
            ImGui::Text("%s", n.title.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - closeBtnSize);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 0.3f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));

            if (ImGui::Button("X", ImVec2(closeBtnSize, closeBtnSize))) {
                n.dismiss = true;
            }
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar();

            ImGui::Spacing();

            ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
            ImGui::TextUnformatted(n.message.c_str());
            ImGui::PopTextWrapPos();
        }

        float height = ImGui::GetWindowHeight();
        nextWindowPos.y -= (height + PADDING);

        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    m_notifications.erase(std::remove_if(m_notifications.begin(), m_notifications.end(),
        [](const Notification& n){ return n.dismiss; }), m_notifications.end());
}