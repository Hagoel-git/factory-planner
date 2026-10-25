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

inline std::optional<std::filesystem::path> getExecutableDirectory() {
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

inline std::string pathToUtf8(const std::filesystem::path& p) {
#ifdef _WIN32
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else
    const std::string& str = p.native();
    // Validate UTF-8
    bool validUtf8 = true;
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c <= 0x7F) {
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= str.size() || (static_cast<unsigned char>(str[i+1]) & 0xC0) != 0x80) {
                validUtf8 = false; break;
            }
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= str.size() || 
                (static_cast<unsigned char>(str[i+1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(str[i+2]) & 0xC0) != 0x80) {
                validUtf8 = false; break;
            }
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= str.size() || 
                (static_cast<unsigned char>(str[i+1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(str[i+2]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(str[i+3]) & 0xC0) != 0x80) {
                validUtf8 = false; break;
            }
            i += 4;
        } else {
            validUtf8 = false;
            break;
        }
    }
    if (validUtf8) {
        return str;
    }
    // Convert non-UTF-8 bytes (e.g. ISO-8859-1 / CP1252 ANSI bytes 0x80-0xFF) to UTF-8
    std::string utf8;
    utf8.reserve(str.size() * 2);
    for (unsigned char c : str) {
        if (c <= 0x7F) {
            utf8.push_back(static_cast<char>(c));
        } else {
            utf8.push_back(static_cast<char>(0xC0 | (c >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return utf8;
#endif
}

inline std::filesystem::path pathFromUtf8(std::string_view utf8Str) {
#ifdef _WIN32
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(utf8Str.data()), utf8Str.size()));
#else
    return std::filesystem::path(std::string(utf8Str));
#endif
}

#endif //FILESYSTEMUTILS_H
