#ifndef FILESYSTEMUTILS_H
#define FILESYSTEMUTILS_H
#include <filesystem>
#include <optional>
#include <vector>
#include <string>
#include <system_error>

#ifdef _WIN32
  #include <windows.h>
#elif __linux__
  #include <unistd.h>
#elif __APPLE__
  #include <mach-o/dyld.h>
#endif

inline std::optional<std::filesystem::path> get_executable_directory() {
    std::filesystem::path exe_path;

#ifdef _WIN32
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) return std::nullopt; // error
        // If len == buf.size() we likely truncated; grow and retry.
        if (static_cast<size_t>(len) < buf.size()) {
            exe_path = std::filesystem::path(std::wstring(buf.data(), len));
            break;
        }
        // grow buffer (guard against insane size)
        if (buf.size() > (1u << 20)) return std::nullopt; // avoid unbounded growth
        buf.resize(buf.size() * 2);
    }

#elif __linux__
    std::vector<char> buf(1024);
    for (;;) {
        ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size());
        if (len == -1) {
            // readlink failed
            return std::nullopt;
        }
        // If return equals buffer size, it may have been truncated — grow and retry
        if (static_cast<size_t>(len) < buf.size()) {
            exe_path = std::filesystem::path(std::string(buf.data(), static_cast<size_t>(len)));
            break;
        }
        if (buf.size() > (1u << 20)) return std::nullopt;
        buf.resize(buf.size() * 2);
    }

#elif __APPLE__
    uint32_t size = 0;
    // First call to get required size
    _NSGetExecutablePath(nullptr, &size); // sets size
    if (size == 0) size = 1024;
    std::vector<char> buf(size);
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        return std::nullopt; // unexpected
    }
    // _NSGetExecutablePath writes a C-string
    exe_path = std::filesystem::path(std::string(buf.data()));
#endif

    if (exe_path.empty()) return std::nullopt;

    // Try to canonicalize (resolve symlinks). If that fails, try absolute() as a fallback so
    // callers get an absolute path when possible.
    std::error_code ec;
    auto canonical_path = std::filesystem::canonical(exe_path, ec);
    std::filesystem::path result_dir;
    if (!ec) {
        result_dir = canonical_path.parent_path();
    } else {
        // fallback: absolute path (doesn't resolve symlinks), and normalize lexically
        auto abs = std::filesystem::absolute(exe_path, ec);
        if (!ec) result_dir = abs.parent_path().lexically_normal();
        else result_dir = exe_path.parent_path().lexically_normal();
    }

    if (result_dir.empty()) return std::nullopt;
    return result_dir;
}

inline std::vector<std::filesystem::path> GetGameDataFiles(const std::filesystem::path &directory) {
    if (!exists(directory)) {
        create_directories(directory);
    }
    std::vector<std::filesystem::path> jsonFiles;
    for (const auto &entry: std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            jsonFiles.push_back(entry.path().filename());
        }
    }
    return jsonFiles;
}

#endif //FILESYSTEMUTILS_H
