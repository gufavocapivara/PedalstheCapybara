#include "plugin_package.h"

#include "miniz.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace gp {
    namespace {
        constexpr std::uint64_t kMaxPackageBytes = 256ull * 1024ull * 1024ull;
        constexpr std::uint64_t kMaxEntryBytes = 64ull * 1024ull * 1024ull;
        constexpr std::uint64_t kMaxTextBytes = 4ull * 1024ull * 1024ull;
        constexpr std::uint32_t kMaxEntries = 8192;
        constexpr std::uint32_t kMaxPackFiles = 2048;
        constexpr std::uint64_t kMaxPackBytes = 256ull * 1024ull * 1024ull;

        std::string trimCopy(std::string s) {
            auto space = [](unsigned char c) { return std::isspace(c) != 0; };
            while (!s.empty() && space((unsigned char)s.front())) s.erase(s.begin());
            while (!s.empty() && space((unsigned char)s.back())) s.pop_back();
            return s;
        }

        bool startsWithNoCase(const std::string& hay, const std::string& needle) {
            if (needle.size() > hay.size()) return false;
            for (size_t i = 0; i < needle.size(); ++i)
                if (std::tolower((unsigned char)hay[i]) != std::tolower((unsigned char)needle[i]))
                    return false;
            return true;
        }

        bool endsWithNoCase(const std::string& hay, const std::string& needle) {
            if (needle.size() > hay.size()) return false;
            const size_t off = hay.size() - needle.size();
            for (size_t i = 0; i < needle.size(); ++i)
                if (std::tolower((unsigned char)hay[off + i]) != std::tolower((unsigned char)needle[i]))
                    return false;
            return true;
        }

        std::string extensionLower(const std::string& name) {
            const auto slash = name.find_last_of("/\\");
            const auto dot = name.find_last_of('.');
            if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
            return lowerAscii(name.substr(dot));
        }

        bool isAssetFolderName(const std::string& dir) {
            const std::string base = lowerAscii(pathBasename(dir));
            return base == "assets" || base == "images" || base == "img" ||
                   (base.size() > 7 && base.compare(base.size() - 7, 7, "_assets") == 0);
        }

        bool isUnderRoot(const std::string& entry, const std::string& root) {
            if (root.empty()) return true;
            return entry.size() > root.size() &&
                   startsWithNoCase(entry, root) && entry[root.size()] == '/';
        }

        bool isDirectChild(const std::string& entry, const std::string& root) {
            if (!isUnderRoot(entry, root)) return false;
            const std::string rest = root.empty() ? entry : entry.substr(root.size() + 1);
            return rest.find('/') == std::string::npos;
        }

        std::string joinRoot(const std::string& root, const std::string& rel) {
            if (root.empty()) return normalizeZipPath(rel);
            if (rel.empty()) return normalizeZipPath(root);
            return normalizeZipPath(root + "/" + rel);
        }

        std::vector<std::string> entriesWithExtension(const PluginPackage& pkg,
                                                      const std::string& ext,
                                                      const std::string& root = {}) {
            std::vector<std::string> out;
            for (const auto& e : pkg.entries()) {
                if (e.isDir) continue;
                if (!isUnderRoot(e.name, root)) continue;
                if (extensionLower(e.name) == ext) out.push_back(e.name);
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        const PackageEntry* findEntryNoCase(const PluginPackage& pkg, const std::string& name) {
            if (const auto* e = pkg.findEntry(name)) return e;
            const std::string lower = lowerAscii(name);
            for (const auto& e : pkg.entries())
                if (!e.isDir && lowerAscii(e.name) == lower) return &e;
            return nullptr;
        }

        std::string chooseScript(const PluginPackage& pkg,
                                 const std::string& root,
                                 const PackageManifest& manifest) {
            if (!manifest.script.empty()) {
                const std::string direct = joinRoot(root, manifest.script);
                if (findEntryNoCase(pkg, direct)) return direct;
                // O manifesto pode apontar so o nome do arquivo.
                const std::string base = pathBasename(manifest.script);
                for (const auto& lua : entriesWithExtension(pkg, ".lua", root))
                    if (lowerAscii(pathBasename(lua)) == lowerAscii(base)) return lua;
            }

            // Sem SCRIPT=: usa o primeiro .lua direto na pasta do plugin.
            const auto luas = entriesWithExtension(pkg, ".lua", root);
            for (const auto& lua : luas) {
                const std::string dir = pathDirname(lua);
                if (isAssetFolderName(dir)) continue;
                if (isDirectChild(lua, root)) return lua;
            }
            for (const auto& lua : luas) {
                if (!isAssetFolderName(pathDirname(lua))) return lua;
            }
            return {};
        }

        PluginCandidate makeManifestCandidate(const std::shared_ptr<PluginPackage>& pkg,
                                              const std::string& root,
                                              const std::string& manifestEntry,
                                              const PackageManifest& manifest) {
            PluginCandidate c;
            c.package = pkg;
            c.rootPrefix = normalizeZipPath(root);
            c.manifestEntry = normalizeZipPath(manifestEntry);
            c.manifest = manifest;
            c.scriptEntry = chooseScript(*pkg, root, manifest);
            if (c.scriptEntry.empty())
                c.error = "SCRIPT not found in the package" + (root.empty() ? std::string() : " (" + root + ")");
            return c;
        }

        PluginCandidate makeImplicitCandidate(const std::shared_ptr<PluginPackage>& pkg,
                                              const std::string& luaEntry) {
            PluginCandidate c;
            c.package = pkg;
            c.rootPrefix = pathDirname(luaEntry);
            c.scriptEntry = normalizeZipPath(luaEntry);
            c.implicitManifest = true;
            const std::string dirName = pathBasename(c.rootPrefix);
            c.manifest.name = dirName.empty() ? pathStem(luaEntry) : dirName;
            c.manifest.script = pathBasename(luaEntry);
            return c;
        }

        void collectPackFiles(const std::filesystem::path& folder,
                              const std::filesystem::path& current,
                              std::vector<PackageFile>& files,
                              std::uint64_t& totalBytes,
                              std::string& error) {
            namespace fs = std::filesystem;
            std::error_code ec;
            static const std::unordered_set<std::string> skipDirs = {
                ".git", ".svn", ".vs", ".vscode", ".idea", "build", "out", "bin", "obj",
                "node_modules", "__pycache__", "cmakefiles"
            };

            std::vector<fs::directory_entry> entries;
            for (fs::directory_iterator it(current, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                entries.push_back(*it);
            }
            if (ec) { error = "Could not read folder: " + ec.message(); return; }

            std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
                return lowerAscii(a.path().filename().string()) < lowerAscii(b.path().filename().string());
            });

            for (const auto& entry : entries) {
                if (!error.empty()) return;
                const std::string name = entry.path().filename().string();
                const std::string lower = lowerAscii(name);

                if (entry.is_directory(ec)) {
                    if (ec) { ec.clear(); continue; }
                    if (skipDirs.count(lower)) continue;
                    collectPackFiles(folder, entry.path(), files, totalBytes, error);
                    if (!error.empty()) return;
                    continue;
                }
                if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }

                if (lower == "thumbs.db" || lower == ".ds_store" || lower == "desktop.ini" ||
                    lower == "pack.ini" || endsWithNoCase(lower, ".pp") ||
                    endsWithNoCase(lower, ".log") || endsWithNoCase(lower, ".tmp"))
                    continue;

                std::error_code sizeEc;
                const auto size = entry.file_size(sizeEc);
                if (sizeEc) continue;
                if (size > kMaxEntryBytes) {
                    error = "File is too large to pack: " + name;
                    return;
                }
                if (files.size() >= kMaxPackFiles) {
                    error = "Too many files in the folder (maximum " + std::to_string(kMaxPackFiles) + ").";
                    return;
                }
                if (totalBytes + size > kMaxPackBytes) {
                    error = "The folder is too large to pack (maximum 256 MB).";
                    return;
                }

                PackageFile file;
                file.name = normalizeZipPath(fs::relative(entry.path(), folder, ec).generic_string());
                if (ec || file.name.empty()) { ec.clear(); continue; }
                std::ifstream in(entry.path(), std::ios::binary);
                if (!in) continue;
                file.data.resize((size_t)size);
                if (size > 0) in.read(reinterpret_cast<char*>(file.data.data()), (std::streamsize)size);
                if (!in && size > 0) continue;
                totalBytes += size;
                files.push_back(std::move(file));
            }
        }
    }  // namespace

    // ------------------------------------------------------------------ utilidades
    std::string lowerAscii(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    std::string normalizeZipPath(const std::string& path) {
        std::string out;
        out.reserve(path.size());
        bool lastSlash = false;
        for (char c : path) {
            char ch = (c == '\\') ? '/' : c;
            if (ch == '/') {
                if (out.empty() || lastSlash) continue;
                lastSlash = true;
                out.push_back('/');
            } else {
                lastSlash = false;
                out.push_back(ch);
            }
        }
        while (!out.empty() && out.front() == '/') out.erase(out.begin());
        while (!out.empty() && out.back() == '/') out.pop_back();
        return out;
    }

    std::string pathBasename(const std::string& path) {
        const auto slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    std::string pathDirname(const std::string& path) {
        const auto slash = path.find_last_of("/\\");
        if (slash == std::string::npos) return {};
        return path.substr(0, slash);
    }

    std::string pathStem(const std::string& path) {
        std::string base = pathBasename(path);
        const auto dot = base.find_last_of('.');
        if (dot != std::string::npos && dot > 0) base.erase(dot);
        return base;
    }

    std::string pathJoin(const std::string& a, const std::string& b) {
        if (a.empty()) return normalizeZipPath(b);
        if (b.empty()) return normalizeZipPath(a);
        return normalizeZipPath(a + "/" + b);
    }

    bool isImageFile(const std::string& name) {
        const std::string ext = extensionLower(name);
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
    }

    bool isLuaFile(const std::string& name) { return extensionLower(name) == ".lua"; }
    bool isPackFile(const std::string& name) { return extensionLower(name) == ".pp"; }
    bool isPackageFile(const std::string& name) {
        const std::string ext = extensionLower(name);
        return ext == ".pctp" || ext == ".pp";
    }

    PackageManifest parseManifest(const std::string& text) {
        PackageManifest m;
        std::istringstream in(text);
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (first && line.size() >= 3 &&
                (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
                (unsigned char)line[2] == 0xBF)
                line.erase(0, 3);
            first = false;
            const std::string trimmed = trimCopy(line);
            if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '[')
                continue;
            const auto eq = trimmed.find('=');
            if (eq == std::string::npos) continue;
            std::string key = lowerAscii(trimCopy(trimmed.substr(0, eq)));
            std::string val = trimCopy(trimmed.substr(eq + 1));
            if (key == "name" || key == "title") m.name = val;
            else if (key == "description" || key == "desc") m.description = val;
            else if (key == "author" || key == "by") m.author = val;
            else if (key == "version") m.version = val;
            else if (key == "script") m.script = normalizeZipPath(val);
            else if (key == "icon") m.icon = normalizeZipPath(val);
        }
        return m;
    }

    // ------------------------------------------------------------------ PluginPackage
    struct PluginPackage::Impl {
        std::vector<unsigned char> bytes;
        mz_zip_archive zip{};
        bool zipOpen = false;
        std::filesystem::path path;
        std::string display;
        std::vector<PackageEntry> entries;
        std::unordered_map<std::string, std::uint32_t> byName;
        std::unordered_map<std::string, std::uint32_t> byLower;
    };

    PluginPackage::PluginPackage() : impl_(new Impl) {}

    PluginPackage::~PluginPackage() {
        if (impl_ && impl_->zipOpen) mz_zip_reader_end(&impl_->zip);
    }

    bool PluginPackage::ok() const { return impl_ && impl_->zipOpen; }
    const std::filesystem::path& PluginPackage::path() const { return impl_->path; }
    const std::string& PluginPackage::displayName() const { return impl_->display; }
    const std::vector<PackageEntry>& PluginPackage::entries() const { return impl_->entries; }

    std::shared_ptr<PluginPackage> PluginPackage::openFile(const std::filesystem::path& path,
                                                           std::string& error) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) { error = "Could not open " + path.filename().string() + "."; return nullptr; }
        if (size > kMaxPackageBytes) {
            error = "Package is too large (maximum 256 MB).";
            return nullptr;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) { error = "Could not open " + path.filename().string() + "."; return nullptr; }
        std::vector<unsigned char> bytes((size_t)size);
        if (size > 0) in.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)size);
        if (!in && size > 0) { error = "Could not read " + path.filename().string() + "."; return nullptr; }
        auto pkg = openMemory(std::move(bytes), path.filename().string(), error);
        if (pkg) pkg->impl_->path = path;
        return pkg;
    }

    std::shared_ptr<PluginPackage> PluginPackage::openMemory(std::vector<unsigned char> bytes,
                                                             std::string displayName,
                                                             std::string& error) {
        auto pkg = std::shared_ptr<PluginPackage>(new PluginPackage);
        auto& im = *pkg->impl_;
        im.display = std::move(displayName);
        im.bytes = std::move(bytes);
        if (im.bytes.empty()) { error = "Package is empty."; return nullptr; }
        if (im.bytes.size() > kMaxPackageBytes) { error = "Package is too large."; return nullptr; }

        if (!mz_zip_reader_init_mem(&im.zip, im.bytes.data(), im.bytes.size(), 0)) {
            error = "This file is not a valid .pctp/.pp package (ZIP).";
            return nullptr;
        }
        im.zipOpen = true;

        const mz_uint count = mz_zip_reader_get_num_files(&im.zip);
        if (count > kMaxEntries) { error = "Package has too many files."; return nullptr; }

        for (mz_uint i = 0; i < count; ++i) {
            mz_zip_archive_file_stat st{};
            if (!mz_zip_reader_file_stat(&im.zip, i, &st)) continue;
            if (st.m_is_directory) continue;
            std::string name = normalizeZipPath(st.m_filename ? st.m_filename : "");
            if (name.empty()) continue;
            PackageEntry e;
            e.name = name;
            e.size = st.m_uncomp_size;
            e.index = i;
            e.isDir = false;
            const std::uint32_t index = (std::uint32_t)im.entries.size();
            im.byName.emplace(name, index);
            im.byLower.emplace(lowerAscii(name), index);
            im.entries.push_back(std::move(e));
        }
        if (im.entries.empty()) { error = "Package has no files."; return nullptr; }
        error.clear();
        return pkg;
    }

    bool PluginPackage::hasEntry(const std::string& name) const {
        return findEntry(name) != nullptr;
    }

    const PackageEntry* PluginPackage::findEntry(const std::string& name) const {
        if (!ok()) return nullptr;
        const std::string normalized = normalizeZipPath(name);
        auto it = impl_->byName.find(normalized);
        if (it != impl_->byName.end()) return &impl_->entries[it->second];
        auto lo = impl_->byLower.find(lowerAscii(normalized));
        if (lo != impl_->byLower.end()) return &impl_->entries[lo->second];
        return nullptr;
    }

    bool PluginPackage::readEntry(const std::string& name,
                                  std::vector<unsigned char>& out,
                                  std::string& error) const {
        const PackageEntry* e = findEntry(name);
        if (!e) { error = "File not found in package: " + name; return false; }
        if (e->size > kMaxEntryBytes) { error = "File is too large: " + e->name; return false; }
        out.resize((size_t)e->size);
        if (e->size == 0) { error.clear(); return true; }
        if (!mz_zip_reader_extract_to_mem(&impl_->zip, e->index, out.data(), out.size(), 0)) {
            error = "Could not read " + e->name + " from the package.";
            return false;
        }
        error.clear();
        return true;
    }

    bool PluginPackage::readText(const std::string& name, std::string& out, std::string& error) const {
        const PackageEntry* e = findEntry(name);
        if (e && e->size > kMaxTextBytes) { error = "Text file is too large: " + name; return false; }
        std::vector<unsigned char> bytes;
        if (!readEntry(name, bytes, error)) return false;
        out.assign(bytes.begin(), bytes.end());
        return true;
    }

    const PackageEntry* PluginPackage::findAsset(const std::string& rawName,
                                                 const std::string& rawModule,
                                                 const std::string& rawRoot,
                                                 const std::string& rawStem) const {
        if (!ok()) return nullptr;
        std::string name = normalizeZipPath(rawName);
        while (startsWithNoCase(name, "./")) name.erase(0, 2);
        if (name.empty()) return nullptr;
        const std::string root = normalizeZipPath(rawRoot);
        const std::string module = normalizeZipPath(rawModule);
        const std::string stem = normalizeZipPath(rawStem);

        std::vector<std::string> candidates;
        auto add = [&](const std::string& p) {
            if (p.empty()) return;
            const std::string n = normalizeZipPath(p);
            if (std::find(candidates.begin(), candidates.end(), n) == candidates.end())
                candidates.push_back(n);
        };

        add(joinRoot(root, name));
        if (!module.empty()) {
            add(joinRoot(root, module + "/" + name));
            add(joinRoot(root, module + "_assets/" + name));
            add(joinRoot(root, "assets/" + module + "/" + name));
        }
        if (!stem.empty()) add(joinRoot(root, stem + "_assets/" + name));
        add(joinRoot(root, "assets/" + name));
        add(joinRoot(root, "images/" + name));

        for (const auto& c : candidates)
            if (const PackageEntry* e = findEntryNoCase(*this, c)) return e;

        // Ultimo recurso: mesmo nome de arquivo em qualquer subpasta do plugin.
        const std::string wanted = lowerAscii(pathBasename(name));
        for (const auto& e : impl_->entries) {
            if (e.isDir || !isUnderRoot(e.name, root)) continue;
            if (lowerAscii(pathBasename(e.name)) == wanted) return &e;
        }
        return nullptr;
    }

    std::vector<std::string> PluginPackage::imageEntries(const std::string& rootPrefix) const {
        std::vector<std::string> out;
        const std::string root = normalizeZipPath(rootPrefix);
        for (const auto& e : impl_->entries) {
            if (e.isDir || !isUnderRoot(e.name, root)) continue;
            if (isImageFile(e.name)) out.push_back(e.name);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // ------------------------------------------------------------------ escrita
    bool writePackage(const std::filesystem::path& outFile,
                      const std::vector<PackageFile>& files,
                      std::string& error) {
        if (files.empty()) { error = "The package has no files."; return false; }

        mz_zip_archive zip{};
        if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
            error = "Could not create the ZIP archive.";
            return false;
        }

        std::unordered_set<std::string> seen;
        for (const auto& f : files) {
            const std::string name = normalizeZipPath(f.name);
            if (name.empty() || name.find("..") != std::string::npos ||
                name.front() == '/') {
                mz_zip_writer_end(&zip);
                error = "Invalid file name in package: " + f.name;
                return false;
            }
            if (!seen.insert(lowerAscii(name)).second) continue;
            if (f.data.size() > kMaxEntryBytes) {
                mz_zip_writer_end(&zip);
                error = "File is too large to pack: " + name;
                return false;
            }
            if (!mz_zip_writer_add_mem(&zip, name.c_str(), f.data.data(), f.data.size(),
                                       MZ_DEFAULT_COMPRESSION)) {
                mz_zip_writer_end(&zip);
                error = "Could not add " + name + " to the package.";
                return false;
            }
        }

        void* buffer = nullptr;
        size_t size = 0;
        if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size) || !buffer) {
            mz_zip_writer_end(&zip);
            error = "Could not finish the ZIP archive.";
            return false;
        }
        mz_zip_writer_end(&zip);

        std::error_code ec;
        if (!outFile.parent_path().empty())
            std::filesystem::create_directories(outFile.parent_path(), ec);
        std::ofstream out(outFile, std::ios::binary);
        if (!out) {
            mz_free(buffer);
            error = "Could not write " + outFile.filename().string() + ".";
            return false;
        }
        out.write(reinterpret_cast<const char*>(buffer), (std::streamsize)size);
        mz_free(buffer);
        if (!out) { error = "Could not write " + outFile.filename().string() + "."; return false; }
        error.clear();
        return true;
    }

    int countPluginsInFolder(const std::filesystem::path& folder) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(folder, ec) || !fs::is_directory(folder, ec)) return 0;

        std::unordered_map<std::string, std::vector<fs::path>> ptcs;
        std::unordered_map<std::string, std::vector<fs::path>> luas;
        int packages = 0;

        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec)) {
                const std::string name = lowerAscii(it->path().filename().string());
                if (name == ".git" || name == ".vs" || name == "build" || name == "out" ||
                    name == "node_modules" || name == "__pycache__")
                    it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            const std::string ext = extensionLower(it->path().filename().string());
            const std::string dir = lowerAscii(it->path().parent_path().string());
            if (ext == ".ptc") ptcs[dir].push_back(it->path());
            else if (ext == ".lua") luas[dir].push_back(it->path());
            else if (ext == ".pctp") ++packages;
        }

        int count = packages;
        for (auto& [dir, list] : ptcs) {
            std::unordered_set<std::string> referenced;
            for (const auto& p : list) {
                ++count;
                std::ifstream in(p, std::ios::binary);
                if (!in) continue;
                std::stringstream ss;
                ss << in.rdbuf();
                const PackageManifest m = parseManifest(ss.str());
                if (!m.script.empty()) referenced.insert(lowerAscii(pathBasename(m.script)));
            }
            auto it = luas.find(dir);
            if (it != luas.end()) {
                for (const auto& lua : it->second)
                    if (!referenced.count(lowerAscii(lua.filename().string()))) ++count;
            }
        }
        for (auto& [dir, list] : luas) {
            if (ptcs.count(dir)) continue;
            count += (int)list.size();
        }
        return count;
    }

    bool writePackFromDirectory(const std::filesystem::path& folder,
                                const std::filesystem::path& outFile,
                                const std::string& packName,
                                const std::string& description,
                                std::string& error,
                                int* pluginCount) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(folder, ec) || !fs::is_directory(folder, ec)) {
            error = "The selected folder does not exist.";
            return false;
        }

        const int count = countPluginsInFolder(folder);
        if (count <= 0) {
            error = "No plugins found. Put a .lua/.ptc/.pctp file (or a folder with them) inside.";
            return false;
        }
        if (pluginCount) *pluginCount = count;

        PackageManifest existing;
        {
            std::ifstream in(folder / "pack.ini", std::ios::binary);
            if (in) {
                std::stringstream ss;
                ss << in.rdbuf();
                existing = parseManifest(ss.str());
            }
        }

        std::vector<PackageFile> files;
        std::uint64_t total = 0;
        collectPackFiles(folder, folder, files, total, error);
        if (!error.empty()) return false;

        PackageFile ini;
        ini.name = "pack.ini";
        std::ostringstream text;
        text << "NAME=" << (packName.empty() ? (existing.name.empty() ? folder.filename().string() : existing.name) : packName) << "\n";
        text << "DESCRIPTION=" << (description.empty() ? existing.description : description) << "\n";
        if (!existing.author.empty()) text << "AUTHOR=" << existing.author << "\n";
        text << "VERSION=" << (existing.version.empty() ? "1.0" : existing.version) << "\n";
        const std::string iniText = text.str();
        ini.data.assign(iniText.begin(), iniText.end());
        files.insert(files.begin(), std::move(ini));

        if (!writePackage(outFile, files, error)) return false;
        error.clear();
        return true;
    }

    // ------------------------------------------------------------------ descoberta
    namespace {
        PackDiscovery discoverRecursive(const std::shared_ptr<PluginPackage>& pkg, bool isPack, int depth);

        void appendCandidates(const std::shared_ptr<PluginPackage>& pkg,
                              const std::string& root,
                              std::vector<PluginCandidate>& out) {
            const auto manifests = entriesWithExtension(*pkg, ".ptc", root);
            std::unordered_set<std::string> claimedRoots;

            for (const auto& entry : manifests) {
                const std::string entryRoot = pathDirname(entry);
                // Um manifesto por pasta de plugin.
                if (!claimedRoots.insert(lowerAscii(entryRoot)).second) continue;

                std::string text;
                std::string err;
                PackageManifest manifest;
                if (pkg->readText(entry, text, err)) manifest = parseManifest(text);
                PluginCandidate c = makeManifestCandidate(pkg, entryRoot, entry, manifest);
                if (c.manifest.name.empty()) {
                    const std::string dir = pathBasename(entryRoot);
                    c.manifest.name = dir.empty() ? pathStem(entry) : dir;
                }
                out.push_back(std::move(c));
            }

            // Pastas com apenas .lua: viram um plugin implicito (manifesto automatico).
            std::unordered_set<std::string> implicitDirs;
            for (const auto& lua : entriesWithExtension(*pkg, ".lua", root)) {
                const std::string dir = pathDirname(lua);
                if (isAssetFolderName(dir)) continue;
                bool underClaimed = false;
                for (const auto& r : claimedRoots)
                    if (isUnderRoot(lua, r)) { underClaimed = true; break; }
                if (underClaimed) continue;
                if (!implicitDirs.insert(lowerAscii(dir)).second) continue;
                // Usa o primeiro .lua direto na pasta (ordem alfabetica).
                if (!isDirectChild(lua, root)) continue;
                out.push_back(makeImplicitCandidate(pkg, lua));
            }
        }

        PackDiscovery discoverRecursive(const std::shared_ptr<PluginPackage>& pkg, bool isPack, int depth) {
            PackDiscovery out;
            if (!pkg) { out.error = "Invalid package."; return out; }

            // pack.ini (opcional) descreve o pack.
            {
                std::string text;
                std::string err;
                if (pkg->hasEntry("pack.ini") && pkg->readText("pack.ini", text, err))
                    out.manifest = parseManifest(text);
            }

            const bool hasPackIni = pkg->hasEntry("pack.ini");
            const bool treatAsPack = isPack || hasPackIni;

            if (!treatAsPack) {
                // .pctp: tenta um plugin na raiz.
                const auto rootManifests = entriesWithExtension(*pkg, ".ptc", "");
                bool hasRootManifest = false;
                for (const auto& m : rootManifests) {
                    if (pathDirname(m).empty()) { hasRootManifest = true; break; }
                }
                if (hasRootManifest) {
                    appendCandidates(pkg, "", out.plugins);
                    if (!out.plugins.empty()) return out;
                }

                // .pctp com um .lua na raiz.
                for (const auto& lua : entriesWithExtension(*pkg, ".lua", "")) {
                    if (pathDirname(lua).empty()) {
                        out.plugins.push_back(makeImplicitCandidate(pkg, lua));
                        break;
                    }
                }
                if (!out.plugins.empty()) return out;
                // Se nao encontrou nada na raiz, continua como pack (auto-deteccao).
            }

            // Pacotes .pctp dentro do pack.
            if (depth < 2) {
                for (const auto& entry : entriesWithExtension(*pkg, ".pctp", "")) {
                    std::vector<unsigned char> bytes;
                    std::string err;
                    if (!pkg->readEntry(entry, bytes, err)) continue;
                    auto inner = PluginPackage::openMemory(std::move(bytes),
                                                           pkg->displayName() + ":" + entry, err);
                    if (!inner) continue;
                    PackDiscovery sub = discoverRecursive(inner, false, depth + 1);
                    for (auto& c : sub.plugins) {
                        // O pacote interno tem seu proprio escopo de assets.
                        out.plugins.push_back(std::move(c));
                    }
                }
            }

            appendCandidates(pkg, "", out.plugins);

            if (out.plugins.empty()) {
                out.error = "No plugins found. A pack must contain folders with .lua/.ptc files or .pctp packages.";
            }
            return out;
        }
    }  // namespace

    PackDiscovery discoverPlugins(std::shared_ptr<PluginPackage> package, bool isPack) {
        return discoverRecursive(package, isPack, 0);
    }

}  // namespace gp
