#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H
#include <vector>
#include <filesystem>

struct SessionState {
    std::vector<std::filesystem::path> openProjectPaths;
    int activeProjectIndex = -1; // -1 means no active project
};

class SessionManager {
public :
    static SessionManager& instance();

    void load();
    void save();

    const SessionState& getSessionState() const;
    void setSessionState(const SessionState& state);
private:
    SessionManager();
    ~SessionManager() = default;

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    void loadSession();
    void saveSession() const;

    SessionState m_state;
};



#endif //SESSIONMANAGER_H
