#include "plugins.h"

#include <system_error>
#include <unordered_set>

namespace gp {
    namespace fs = std::filesystem;

    namespace {
        std::string extensionLower(const fs::path& p) {
            return lowerAscii(p.extension().string());
        }

        bool sameFolder(const fs::path& a, const fs::path& b) {
            std::error_code ec;
            return fs::equivalent(a, b, ec) && !ec;
        }

        bool hasUsableScript(const PackDiscovery& disc) {
            for (const auto& c : disc.plugins)
                if (!c.scriptEntry.empty()) return true;
            return false;
        }
    }  // namespace

    bool PluginManager::readFileText(const fs::path& path, std::string& out) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::stringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    fs::path PluginManager::uniquePath(const fs::path& wanted) {
        std::error_code ec;
        if (!fs::exists(wanted, ec)) return wanted;
        const fs::path parent = wanted.parent_path();
        const std::string stem = wanted.stem().string();
        const std::string ext = wanted.extension().string();
        for (int i = 2; i < 10000; ++i) {
            const fs::path candidate = parent / (stem + "_" + std::to_string(i) + ext);
            if (!fs::exists(candidate, ec)) return candidate;
        }
        return wanted;
    }

    void PluginManager::copyAssetsFolder(const fs::path& fromDir,
                                         const fs::path& toDir,
                                         const std::string& stem) {
        std::error_code ec;
        const fs::path names[] = {
            fromDir / (stem + "_assets"),
            fromDir / "assets",
            fromDir / "images"
        };
        for (const auto& from : names) {
            if (!fs::exists(from, ec) || !fs::is_directory(from, ec)) continue;
            const fs::path to = toDir / from.filename();
            fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        }
    }

    void PluginManager::addError(const std::string& name, const std::string& file,
                                 const std::string& error, bool isPack) {
        PluginInfo info;
        info.name = name.empty() ? file : name;
        info.file = file;
        info.isPackage = isPack;
        info.fromPack = isPack;
        info.packFile = isPack ? file : "";
        info.error = error;
        info.status = error;
        plugins_.push_back(std::move(info));
    }

    void PluginManager::scanFolderPlugins() {
        std::error_code ec;
        std::unordered_set<std::string> dirsWithManifest;
        std::unordered_set<std::string> referencedScripts;
        std::vector<fs::path> manifests;
        std::vector<fs::path> luas;

        auto skipDir = [](const std::string& raw) {
            const std::string d = lowerAscii(raw);
            return d == ".git" || d == ".svn" || d == ".vs" || d == ".vscode" || d == ".idea" ||
                   d == "build" || d == "out" || d == "bin" || d == "obj" ||
                   d == "node_modules" || d == "__pycache__" || d == "assets" ||
                   d == "images" || d == "img" ||
                   (d.size() > 7 && d.compare(d.size() - 7, 7, "_assets") == 0);
        };

        for (fs::recursive_directory_iterator it(dir_, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec)) {
                if (skipDir(it->path().filename().string())) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            const std::string ext = extensionLower(it->path());
            if (ext == ".ptc") manifests.push_back(it->path());
            else if (ext == ".lua") luas.push_back(it->path());
        }
        std::sort(manifests.begin(), manifests.end());
        std::sort(luas.begin(), luas.end());

        for (const auto& path : manifests) {
            std::string text;
            if (!readFileText(path, text)) continue;
            const PackageManifest m = parseManifest(text);
            const fs::path folder = path.parent_path();
            dirsWithManifest.insert(lowerAscii(folder.string()));

            PluginInfo info;
            info.file = fs::relative(path, dir_).generic_string();
            info.manifestEntry = info.file;
            info.name = m.name.empty() ? path.stem().string() : m.name;
            info.description = m.description;
            info.author = m.author;
            info.version = m.version;
            info.script = m.script;
            info.iconEntry = m.icon;

            if (info.script.empty()) {
                // Manifesto antigo sem SCRIPT=: usa o primeiro .lua da pasta.
                std::vector<fs::path> local;
                for (fs::directory_iterator lit(folder, ec), lend; !ec && lit != lend; lit.increment(ec))
                    if (lit->is_regular_file(ec) && extensionLower(lit->path()) == ".lua")
                        local.push_back(lit->path());
                std::sort(local.begin(), local.end());
                if (!local.empty()) info.script = local.front().filename().string();
            }
            if (!info.script.empty()) {
                fs::path scriptPath = folder / info.script;
                if (!fs::exists(scriptPath, ec)) scriptPath = folder / pathBasename(info.script);
                info.scriptPath = scriptPath;
                info.script = fs::relative(scriptPath, dir_, ec).generic_string();
                if (ec) { ec.clear(); info.script = scriptPath.filename().string(); }
                referencedScripts.insert(lowerAscii(scriptPath.lexically_normal().string()));
                if (!fs::exists(scriptPath, ec))
                    info.status = "SCRIPT file not found: " + info.script;
            } else {
                info.status = "No SCRIPT= in the manifest and no .lua file in the folder.";
            }
            plugins_.push_back(std::move(info));
        }

        // Pastas com apenas .lua (sem .ptc): cada .lua vira um plugin implicito.
        for (const auto& path : luas) {
            const std::string folderKey = lowerAscii(path.parent_path().string());
            if (dirsWithManifest.count(folderKey)) continue;
            if (referencedScripts.count(lowerAscii(path.lexically_normal().string()))) continue;

            PluginInfo info;
            info.file = fs::relative(path, dir_).generic_string();
            info.name = path.stem().string();
            info.script = info.file;
            info.scriptPath = path;
            info.implicitManifest = true;
            plugins_.push_back(std::move(info));
        }
    }

    void PluginManager::scanPackageFile(const fs::path& path, bool isPack) {
        std::string error;
        auto pkg = PluginPackage::openFile(path, error);
        if (!pkg) {
            addError(path.stem().string(), path.filename().string(), error, isPack);
            return;
        }

        const PackDiscovery disc = discoverPlugins(pkg, isPack);
        if (disc.plugins.empty()) {
            addError(disc.manifest.name.empty() ? path.stem().string() : disc.manifest.name,
                     path.filename().string(),
                     disc.error.empty() ? "No plugins found in this package." : disc.error,
                     isPack);
            return;
        }

        const std::string packName = isPack
            ? (disc.manifest.name.empty() ? path.stem().string() : disc.manifest.name)
            : std::string();

        for (const auto& c : disc.plugins) {
            PluginInfo info;
            info.file = path.filename().string();
            info.packName = packName;
            info.packFile = isPack ? path.filename().string() : "";
            info.fromPack = isPack;
            info.isPackage = true;
            info.package = c.package;
            info.rootPrefix = c.rootPrefix;
            info.script = c.scriptEntry;
            info.manifestEntry = c.manifestEntry;
            info.iconEntry = c.manifest.icon;
            info.implicitManifest = c.implicitManifest;
            info.name = c.manifest.name;
            if (info.name.empty()) {
                const std::string dir = pathBasename(c.rootPrefix);
                info.name = !dir.empty() ? dir
                           : !c.scriptEntry.empty() ? pathStem(c.scriptEntry)
                           : path.stem().string();
            }
            info.description = c.manifest.description;
            info.author = c.manifest.author;
            info.version = c.manifest.version;
            info.error = c.error;
            info.status = c.error;
            plugins_.push_back(std::move(info));
        }
    }

    void PluginManager::refresh() {
        plugins_.clear();
        std::error_code ec;
        fs::create_directories(dir_, ec);
        fs::create_directories(packsDir_, ec);
        if (ec) return;

        scanFolderPlugins();

        auto scanPackagesIn = [&](const fs::path& folder) {
            std::error_code sec;
            std::vector<fs::path> files;
            for (fs::directory_iterator it(folder, sec), end; !sec && it != end; it.increment(sec)) {
                if (!it->is_regular_file(sec)) continue;
                const std::string ext = extensionLower(it->path());
                if (ext == ".pctp" || ext == ".pp") files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            for (const auto& file : files) {
                const bool isPack = extensionLower(file) == ".pp";
                scanPackageFile(file, isPack);
            }
        };

        scanPackagesIn(dir_);
        if (!sameFolder(dir_, packsDir_)) scanPackagesIn(packsDir_);

        std::sort(plugins_.begin(), plugins_.end(), [](const PluginInfo& a, const PluginInfo& b) {
            const std::string ap = lowerAscii(a.packName);
            const std::string bp = lowerAscii(b.packName);
            if (ap != bp) return ap < bp;
            return lowerAscii(a.name) < lowerAscii(b.name);
        });
    }

    bool PluginManager::createPackFromFolder(const fs::path& folder,
                                             const fs::path& outFile,
                                             const std::string& name,
                                             const std::string& description,
                                             std::string& error,
                                             int* pluginCount) {
        return writePackFromDirectory(folder, outFile, name, description, error, pluginCount);
    }

    static bool removePathSafe(const fs::path& p, std::string& error) {
        std::error_code ec;
        if (p.empty()) { error = "Invalid path."; return false; }
        if (!fs::exists(p, ec)) { error.clear(); return true; }
        fs::remove_all(p, ec);
        if (ec) { error = "Could not remove " + p.filename().string() + ": " + ec.message(); return false; }
        return true;
    }

    bool PluginManager::removePack(const std::string& packFile, std::string& error) {
        if (packFile.empty()) { error = "Pack file is empty."; return false; }
        const fs::path p1 = packsDir_ / packFile;
        const fs::path p2 = dir_ / packFile;
        std::error_code ec;
        bool removed = false;
        for (const auto& p : {p1, p2}) {
            if (fs::exists(p, ec)) {
                if (!removePathSafe(p, error)) return false;
                removed = true;
            }
        }
        if (!removed) { error = "Pack file not found: " + packFile; return false; }
        refresh();
        return true;
    }

    bool PluginManager::removePlugin(const PluginInfo& info, std::string& error) {
        // Pack/plugin inside a .pp: remove the whole pack file.
        if (info.fromPack && !info.packFile.empty())
            return removePack(info.packFile, error);

        std::error_code ec;
        if (info.isPackage) {
            const fs::path p1 = dir_ / info.file;
            const fs::path p2 = packsDir_ / info.file;
            bool removed = false;
            for (const auto& p : {p1, p2}) {
                if (fs::exists(p, ec)) {
                    if (!removePathSafe(p, error)) return false;
                    removed = true;
                }
            }
            if (!removed) { error = "Package file not found: " + info.file; return false; }
            refresh();
            return true;
        }

        // Folder plugin: remove manifest/script and associated asset folders.
        const fs::path base = dir_;
        auto safeInPlugins = [&](const fs::path& p) -> bool {
            std::error_code ec2;
            const fs::path absBase = fs::weakly_canonical(base, ec2);
            const fs::path absP = fs::weakly_canonical(p, ec2);
            const auto bs = absBase.string();
            const auto ps = absP.string();
            return ps.size() > bs.size() && ps.compare(0, bs.size(), bs) == 0;
        };

        std::vector<fs::path> targets;
        if (!info.manifestEntry.empty()) targets.push_back(base / info.manifestEntry);
        if (!info.scriptPath.empty()) targets.push_back(info.scriptPath);
        else if (!info.script.empty()) targets.push_back(base / info.script);

        // Also remove asset folders named <stem>_assets next to script/manifest.
        auto addAssets = [&](const fs::path& f) {
            if (f.empty()) return;
            const fs::path parent = f.parent_path();
            const std::string stem = f.stem().string();
            for (const auto& n : { stem + "_assets", std::string("assets"), std::string("images") }) {
                const fs::path a = parent / n;
                if (fs::exists(a, ec) && fs::is_directory(a, ec)) targets.push_back(a);
            }
        };
        if (!info.scriptPath.empty()) addAssets(info.scriptPath);
        if (!info.manifestEntry.empty()) addAssets(base / info.manifestEntry);

        bool removed = false;
        for (const auto& t : targets) {
            if (t.empty()) continue;
            if (!safeInPlugins(t)) continue;
            if (!fs::exists(t, ec)) continue;
            if (!removePathSafe(t, error)) return false;
            removed = true;
        }
        if (!removed) { error = "Plugin files not found."; return false; }
        refresh();
        return true;
    }

    bool PluginManager::importFile(const fs::path& source,
                                   std::string& error,
                                   fs::path* installed) {
        std::error_code ec;
        if (!fs::exists(source, ec)) { error = "File not found: " + source.filename().string(); return false; }
        if (installed) installed->clear();

        // Pasta inteira -> plugin pack .pp
        if (fs::is_directory(source, ec)) {
            fs::create_directories(packsDir_, ec);
            const std::string name = source.filename().string();
            fs::path dest = uniquePath(packsDir_ / (name + ".pp"));
            int count = 0;
            if (!createPackFromFolder(source, dest, name, "", error, &count)) return false;
            refresh();
            if (installed) *installed = dest;
            return true;
        }

        const std::string ext = extensionLower(source);

        if (ext == ".pp" || ext == ".pctp") {
            auto pkg = PluginPackage::openFile(source, error);
            if (!pkg) return false;
            const bool isPack = (ext == ".pp");
            const PackDiscovery disc = discoverPlugins(pkg, isPack);
            if (disc.plugins.empty() || !hasUsableScript(disc)) {
                error = disc.error.empty() ? "No plugin scripts found in this package." : disc.error;
                return false;
            }

            fs::path targetDir = isPack ? packsDir_ : dir_;
            fs::create_directories(targetDir, ec);
            if (sameFolder(source.parent_path(), dir_) || sameFolder(source.parent_path(), packsDir_)) {
                refresh();
                if (installed) *installed = source;
                return true;
            }
            fs::path dest = uniquePath(targetDir / source.filename());
            fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
            if (ec) { error = "Could not copy the package: " + ec.message(); return false; }
            refresh();
            if (installed) *installed = dest;
            return true;
        }

        if (ext == ".ptc") {
            fs::create_directories(dir_, ec);
            std::string text;
            if (!readFileText(source, text)) { error = "Could not read the .ptc manifest."; return false; }
            const PackageManifest m = parseManifest(text);

            fs::path dest = uniquePath(dir_ / source.filename());
            fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
            if (ec) { error = "Could not copy the manifest: " + ec.message(); return false; }

            std::string script = m.script;
            if (script.empty()) {
                for (fs::directory_iterator it(source.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
                    if (it->is_regular_file(ec) && extensionLower(it->path()) == ".lua") {
                        script = it->path().filename().string();
                        break;
                    }
                }
            }
            if (!script.empty()) {
                const fs::path fromScript = source.parent_path() / script;
                if (fs::exists(fromScript, ec)) {
                    fs::path toScript = dest.parent_path() / pathBasename(script);
                    bool renamed = false;
                    if (fs::exists(toScript, ec)) {
                        const fs::path unique = uniquePath(toScript);
                        renamed = (unique != toScript);
                        toScript = unique;
                    }
                    fs::copy_file(fromScript, toScript, fs::copy_options::overwrite_existing, ec);
                    if (ec) { error = "Could not copy the Lua script: " + ec.message(); return false; }
                    if (renamed) {
                        std::ofstream extra(dest, std::ios::app | std::ios::binary);
                        if (extra) extra << "\nSCRIPT=" << toScript.filename().string() << "\n";
                    }
                    copyAssetsFolder(source.parent_path(), dest.parent_path(), pathStem(toScript.filename().string()));
                }
                copyAssetsFolder(source.parent_path(), dest.parent_path(), pathStem(script));
            }
            copyAssetsFolder(source.parent_path(), dest.parent_path(), source.stem().string());
            refresh();
            if (installed) *installed = dest;
            return true;
        }

        if (ext == ".lua") {
            fs::create_directories(dir_, ec);
            fs::path dest = uniquePath(dir_ / source.filename());
            fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
            if (ec) { error = "Could not copy the Lua script: " + ec.message(); return false; }

            const std::string sourceStem = source.stem().string();
            const std::string destStem = dest.stem().string();
            const fs::path manifest = dest.parent_path() / (destStem + ".ptc");
            if (!fs::exists(manifest, ec)) {
                std::ofstream out(manifest, std::ios::binary);
                if (out) {
                    out << "NAME=" << destStem << "\n"
                        << "DESCRIPTION=Imported Lua plugin\n"
                        << "SCRIPT=" << dest.filename().string() << "\n";
                }
            }
            // Se o arquivo foi renomeado (foo_2.lua), os assets tambem precisam
            // acompanhar o novo nome para loadassets() encontrar as imagens.
            copyAssetsFolder(source.parent_path(), dest.parent_path(), destStem);
            if (destStem != sourceStem) {
                const fs::path fromAssets = source.parent_path() / (sourceStem + "_assets");
                const fs::path toAssets = dest.parent_path() / (destStem + "_assets");
                if (fs::exists(fromAssets, ec) && !fs::exists(toAssets, ec))
                    fs::copy(fromAssets, toAssets, fs::copy_options::recursive, ec);
            }
            copyAssetsFolder(source.parent_path(), dest.parent_path(), sourceStem);
            refresh();
            if (installed) *installed = dest;
            return true;
        }

        error = "Unsupported file. Use .pctp, .pp, .ptc, .lua or a folder.";
        return false;
    }

}  // namespace gp
