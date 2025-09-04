#ifndef RECENTFILES_H
#define RECENTFILES_H
#include <filesystem>
#include <vector>


class RecentFiles {
public:
    static RecentFiles& instance();

    void load();
    void save();

    void addFile(const std::filesystem::path& filePath);
    const std::vector<std::filesystem::path>& getFiles() const;

private:
    RecentFiles () = default;
    ~RecentFiles() = default;

    RecentFiles (const RecentFiles&) = delete;
    RecentFiles& operator=(const RecentFiles&) = delete;

    void loadRecentFiles();
    void saveRecentFiles() const;

    std::vector<std::filesystem::path> files;
};



#endif //RECENTFILES_H
