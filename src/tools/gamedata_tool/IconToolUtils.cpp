#include "tools/gamedata_tool/IconToolUtils.h"

#include "common/FilesystemUtils.h"
#include "common/StringUtils.h"
#include "core/data/PngInfo.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace gamedata_tool {

namespace {

std::string relativeDisplayPath(
    const std::filesystem::path& base,
    const std::filesystem::path& target,
    std::string_view fallback) {
    if (base.empty()) {
        return std::string(fallback);
    }
    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(target, base, ec);
    if (!ec && !rel.empty()) {
        return rel.generic_string();
    }
    return std::string(fallback);
}

bool pathsReferToSameLocation(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a.empty() || b.empty()) return false;
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec)) {
        return true;
    }
    return a.lexically_normal() == b.lexically_normal();
}

gamedata::LoadDiagnostic makeIconError(
    std::string file,
    std::string jsonPointer,
    std::string subject,
    std::string message) {
    gamedata::LoadDiagnostic d;
    d.severity = gamedata::DiagnosticSeverity::Error;
    d.rule_id = "V-ICON-1";
    d.file = std::move(file);
    d.json_pointer = std::move(jsonPointer);
    d.subject = std::move(subject);
    d.message = std::move(message);
    d.formatted = d.message;
    return d;
}

gamedata::LoadDiagnostic makeIconMissingWarning(
    std::string file,
    std::string jsonPointer,
    std::string subject) {
    gamedata::LoadDiagnostic d;
    d.severity = gamedata::DiagnosticSeverity::Warning;
    d.rule_id = "V-W-1";
    d.message = file + " not found";
    d.formatted = d.message;
    d.file = std::move(file);
    d.json_pointer = std::move(jsonPointer);
    d.subject = std::move(subject);
    return d;
}

} // namespace

std::string toAsciiLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string formatCountWithCommas(uint64_t value) {
    std::string raw = std::to_string(value);
    std::string out;
    out.reserve(raw.size() + raw.size() / 3);
    for (size_t i = 0; i < raw.size(); ++i) {
        if (i > 0 && (raw.size() - i) % 3 == 0) {
            out.push_back(',');
        }
        out.push_back(raw[i]);
    }
    return out;
}

std::string makeNoIconsNote(const gamedata::GamePackage& pkg) {
    return "package has no icons: " + formatCountWithCommas(pkg.resources.size()) +
           " resources and " + formatCountWithCommas(pkg.machines.size()) +
           " machines use fallback icons";
}

std::string formatVIcon1Message(
    std::string_view label,
    uint32_t w,
    uint32_t h,
    std::string_view parseError) {
    std::string out(label);
    if (!parseError.empty()) {
        out += ": not a valid PNG (";
        out += parseError;
        out += ")";
        return out;
    }
    out += ": ";
    out += std::to_string(w);
    out += "x";
    out += std::to_string(h);
    out += " (must be square and <= 64x64)";
    return out;
}

std::vector<ReferencedIcon> collectReferencedIcons(const gamedata::GamePackage& pkg) {
    std::vector<ReferencedIcon> refs;
    size_t optionCount = 0;
    for (const auto& oset : pkg.option_sets) {
        optionCount += oset.options.size();
    }
    refs.reserve(pkg.resources.size() + pkg.machines.size() + optionCount);

    for (size_t i = 0; i < pkg.resources.size(); ++i) {
        const auto& r = pkg.resources[i];
        ReferencedIcon ref;
        ref.kind = ReferencedIcon::Kind::Resource;
        ref.key = "resources/" + r.id;
        ref.name_slug = slugify(r.name);
        ref.rel_png = ref.key + ".png";
        ref.json_pointer = "/resources/" + std::to_string(i);
        refs.push_back(std::move(ref));
    }

    for (size_t i = 0; i < pkg.machines.size(); ++i) {
        const auto& m = pkg.machines[i];
        ReferencedIcon ref;
        ref.kind = ReferencedIcon::Kind::Machine;
        ref.key = "machines/" + m.id;
        ref.name_slug = slugify(m.name);
        ref.rel_png = ref.key + ".png";
        ref.json_pointer = "/machines/" + std::to_string(i);
        refs.push_back(std::move(ref));
    }

    for (const auto& oset : pkg.option_sets) {
        for (size_t i = 0; i < oset.options.size(); ++i) {
            const auto& opt = oset.options[i];
            ReferencedIcon ref;
            ref.kind = ReferencedIcon::Kind::Option;
            ref.key = "options/" + oset.id + "/" + opt.key;
            ref.name_slug.clear(); // Empty for options per §3
            ref.rel_png = ref.key + ".png";
            ref.json_pointer = "/option_sets/" + oset.id + "/options/" + std::to_string(i);
            refs.push_back(std::move(ref));
        }
    }

    return refs;
}

void openIconSources(
    const gamedata::GamePackage& pkg,
    IconSources& outSources,
    std::vector<gamedata::LoadDiagnostic>& outErrors) {
    outSources = IconSources{};
    std::error_code ec;

    if (!pkg.package_root.empty()) {
        outSources.root_pak_path = pkg.package_root / "icons.pak";
        outSources.has_root_pak = std::filesystem::is_regular_file(outSources.root_pak_path, ec);
    }

    if (!pkg.icon_root.empty()) {
        outSources.loose_dir = pkg.icon_root;
        outSources.has_loose_dir = std::filesystem::is_directory(outSources.loose_dir, ec);

        std::filesystem::path candidateIconPak = pkg.icon_root / "icons.pak";
        if (!pathsReferToSameLocation(candidateIconPak, outSources.root_pak_path)) {
            outSources.icon_pak_path = std::move(candidateIconPak);
            outSources.has_icon_pak = std::filesystem::is_regular_file(outSources.icon_pak_path, ec);
            outSources.icon_pak_display = relativeDisplayPath(
                pkg.package_root, outSources.icon_pak_path, "icons/icons.pak");
        }
    }

    if (outSources.has_root_pak) {
        gamedata::IconPakReader reader;
        std::string err;
        if (gamedata::IconPakReader::open(outSources.root_pak_path, reader, &err)) {
            outSources.root_pak = std::move(reader);
        } else {
            outErrors.push_back(makeIconError(
                outSources.root_pak_display,
                "",
                outSources.root_pak_display,
                "corrupt " + outSources.root_pak_display + ": " + err));
        }
    }

    if (outSources.has_icon_pak) {
        gamedata::IconPakReader reader;
        std::string err;
        if (gamedata::IconPakReader::open(outSources.icon_pak_path, reader, &err)) {
            outSources.icon_pak = std::move(reader);
        } else {
            outErrors.push_back(makeIconError(
                outSources.icon_pak_display,
                "",
                outSources.icon_pak_display,
                "corrupt " + outSources.icon_pak_display + ": " + err));
        }
    }
}

ScannedIconDir scanIconDirectory(const std::filesystem::path& baseDir) {
    ScannedIconDir result;
    std::error_code ec;
    if (baseDir.empty() || !std::filesystem::is_directory(baseDir, ec)) {
        return result;
    }

    std::vector<ScannedFile> rawFiles;
    for (std::filesystem::recursive_directory_iterator it(
             baseDir, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         it != end;
         it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        std::filesystem::path rel = std::filesystem::relative(it->path(), baseDir, ec);
        if (ec || rel.empty()) {
            continue;
        }
        std::string relStr = rel.generic_string();
        if (relStr == "icons.pak") {
            continue;
        }

        ScannedFile sf;
        sf.rel_path = std::move(relStr);
        sf.lower_rel_path = toAsciiLower(sf.rel_path);
        sf.full_path = it->path();
        sf.mtime = it->last_write_time(ec);
        if (!ec) {
            if (!result.newest_mtime.has_value() || sf.mtime > *result.newest_mtime) {
                result.newest_mtime = sf.mtime;
            }
        }
        const std::string extLower = toAsciiLower(it->path().extension().generic_string());
        sf.has_png_ext = (extLower == ".png");
        rawFiles.push_back(std::move(sf));
    }

    std::sort(rawFiles.begin(), rawFiles.end(), [](const ScannedFile& a, const ScannedFile& b) {
        return a.rel_path < b.rel_path;
    });

    result.files = std::move(rawFiles);
    result.by_exact_rel.reserve(result.files.size());
    result.by_lower_rel.reserve(result.files.size());
    for (size_t i = 0; i < result.files.size(); ++i) {
        result.by_exact_rel.emplace(result.files[i].rel_path, i);
        result.by_lower_rel.emplace(result.files[i].lower_rel_path, i);
    }

    return result;
}

void checkPackageIconsForValidate(const gamedata::GamePackage& pkg, gamedata::LoadReport& report) {
    IconSources sources;
    std::vector<gamedata::LoadDiagnostic> pakErrors;
    openIconSources(pkg, sources, pakErrors);

    if (sources.noIconsMode()) {
        report.addNote(makeNoIconsNote(pkg));
        return;
    }

    for (auto& err : pakErrors) {
        report.errors.push_back(std::move(err));
    }

    const std::string looseDisplayPrefix = relativeDisplayPath(
        pkg.package_root, sources.loose_dir, "icons");

    const std::vector<ReferencedIcon> refs = collectReferencedIcons(pkg);
    std::error_code ec;

    for (const auto& ref : refs) {
        // Lookup order T8: <package_root>/icons.pak -> <icon_root>/icons.pak -> loose <icon_root>/<key>.png
        if (sources.root_pak.has_value()) {
            if (const gamedata::IconPakEntry* entry = sources.root_pak->find(ref.key)) {
                if (!gamedata::isValidIconDimensions(entry->width, entry->height)) {
                    report.errors.push_back(makeIconError(
                        sources.root_pak_display,
                        ref.json_pointer,
                        ref.key,
                        formatVIcon1Message(ref.key, entry->width, entry->height, "")));
                }
                continue;
            }
        }

        if (sources.icon_pak.has_value()) {
            if (const gamedata::IconPakEntry* entry = sources.icon_pak->find(ref.key)) {
                if (!gamedata::isValidIconDimensions(entry->width, entry->height)) {
                    report.errors.push_back(makeIconError(
                        sources.icon_pak_display,
                        ref.json_pointer,
                        ref.key,
                        formatVIcon1Message(ref.key, entry->width, entry->height, "")));
                }
                continue;
            }
        }

        const std::string displayFile = (looseDisplayPrefix.empty() || looseDisplayPrefix == ".")
            ? ref.rel_png
            : (looseDisplayPrefix + "/" + ref.rel_png);

        if (sources.has_loose_dir) {
            const std::filesystem::path fullPath = sources.loose_dir / ref.rel_png;
            if (std::filesystem::is_regular_file(fullPath, ec)) {
                const gamedata::PngParseResult png = gamedata::readPngInfoFromFile(fullPath);
                if (!png.ok) {
                    report.errors.push_back(makeIconError(
                        displayFile,
                        ref.json_pointer,
                        ref.key,
                        formatVIcon1Message(displayFile, 0, 0, png.error)));
                } else if (!gamedata::isValidIconDimensions(png.info.width, png.info.height)) {
                    report.errors.push_back(makeIconError(
                        displayFile,
                        ref.json_pointer,
                        ref.key,
                        formatVIcon1Message(displayFile, png.info.width, png.info.height, "")));
                }
                continue;
            }
        }

        if (pkg.isWarningWaived("V-W-1", ref.key)) {
            report.recordWaivedWarning("V-W-1");
            continue;
        }

        report.warnings.push_back(makeIconMissingWarning(displayFile, ref.json_pointer, ref.key));
    }

    report.syncWaivedWarningsNote();
}

PackageIconCoverage computePackageIconCoverage(const gamedata::GamePackage& pkg) {
    PackageIconCoverage cov;
    cov.resources_total = pkg.resources.size();
    cov.machines_total = pkg.machines.size();
    for (const auto& oset : pkg.option_sets) {
        cov.options_total += oset.options.size();
    }

    IconSources sources;
    std::vector<gamedata::LoadDiagnostic> pakErrors;
    openIconSources(pkg, sources, pakErrors);
    (void)pakErrors;

    if (sources.noIconsMode()) {
        cov.mode = "none";
        return cov;
    }

    cov.mode = (sources.has_root_pak || sources.has_icon_pak) ? "pak" : "loose";

    const std::vector<ReferencedIcon> refs = collectReferencedIcons(pkg);
    std::error_code ec;
    for (const auto& ref : refs) {
        bool present = false;
        if (sources.root_pak.has_value() && sources.root_pak->contains(ref.key)) {
            present = true;
        } else if (sources.icon_pak.has_value() && sources.icon_pak->contains(ref.key)) {
            present = true;
        } else if (sources.has_loose_dir &&
                   std::filesystem::is_regular_file(sources.loose_dir / ref.rel_png, ec)) {
            present = true;
        }

        if (present) {
            switch (ref.kind) {
                case ReferencedIcon::Kind::Resource:
                    ++cov.resources_present;
                    break;
                case ReferencedIcon::Kind::Machine:
                    ++cov.machines_present;
                    break;
                case ReferencedIcon::Kind::Option:
                    ++cov.options_present;
                    break;
            }
        } else if (pkg.isWarningWaived("V-W-1", ref.key)) {
            switch (ref.kind) {
                case ReferencedIcon::Kind::Resource:
                    ++cov.resources_waived;
                    break;
                case ReferencedIcon::Kind::Machine:
                    ++cov.machines_waived;
                    break;
                case ReferencedIcon::Kind::Option:
                    ++cov.options_waived;
                    break;
            }
        }
    }

    return cov;
}

} // namespace gamedata_tool
