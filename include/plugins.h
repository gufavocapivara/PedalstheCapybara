// plugins.h - Varre a pasta "Plugins" e lê os arquivos .ptc (NAME=, DESCRIPTION=, SCRIPT=).
#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gp {

    struct PluginInfo {
        std::string name;
        std::string description;
        std::string file;     // nome do arquivo .ptc
        std::string script;   // SCRIPT= (arquivo .lua, relativo à pasta Plugins)
        mutable std::string status;  // "" = sem script, "ok" ou mensagem de erro
    };

    class PluginManager {
    public:
        explicit PluginManager(std::filesystem::path dir = "Plugins") : dir_(std::move(dir)) {
            refresh();
        }

        void refresh() {
            namespace fs = std::filesystem;
            std::error_code ec;
            plugins_.clear();

            if (!fs::exists(dir_, ec)) fs::create_directories(dir_, ec);
            if (ec) return;

            for (const auto& entry : fs::directory_iterator(dir_, ec)) {
                if (ec) break;
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".ptc") continue;

                PluginInfo info;
                if (parse(entry.path(), info)) plugins_.push_back(std::move(info));
            }
            std::sort(plugins_.begin(), plugins_.end(),
                [](const PluginInfo& a, const PluginInfo& b) { return a.name < b.name; });
        }

        const std::vector<PluginInfo>& all() const { return plugins_; }
        const std::filesystem::path& dir() const { return dir_; }

    private:
        static std::string trim(std::string s) {
            auto sp = [](unsigned char c) { return std::isspace(c) != 0; };
            while (!s.empty() && sp((unsigned char)s.front())) s.erase(s.begin());
            while (!s.empty() && sp((unsigned char)s.back())) s.pop_back();
            return s;
        }

        static bool parse(const std::filesystem::path& path, PluginInfo& out) {
            std::ifstream f(path);
            if (!f) return false;

            std::string line;
            bool first = true;
            while (std::getline(f, line)) {
                if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
                    (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                    line.erase(0, 3);
                first = false;

                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string key = trim(line.substr(0, eq));
                std::string val = trim(line.substr(eq + 1));

                std::transform(key.begin(), key.end(), key.begin(),
                    [](unsigned char c) { return (char)std::toupper(c); });

                if (key == "NAME") out.name = val;
                else if (key == "DESCRIPTION") out.description = val;
                else if (key == "SCRIPT") out.script = val;
            }

            out.file = path.filename().string();
            if (out.name.empty()) out.name = path.stem().string();
            return true;
        }

        std::filesystem::path dir_;
        std::vector<PluginInfo> plugins_;
    };

}  // namespace gp