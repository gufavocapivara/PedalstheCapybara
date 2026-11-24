// plugins.h - Varre a pasta Plugins e os packs .pp/.pctp.
//
// Formatos suportados:
//   Plugins/*.ptc  + .lua  (plugin em pasta, formato antigo)
//   Plugins/*.lua          (plugin avulso: o manifesto e criado na importacao)
//   Plugins/*.pctp         (um plugin em ZIP: manifesto + script + imagens)
//   Plugins/*.pp           (plugin pack em ZIP com varios plugins)
//   Packs/*.pp             (idem, pasta separada para packs)
#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "plugin_package.h"

namespace gp {

    struct PluginInfo {
        std::string name;
        std::string description;
        std::string author;
        std::string version;

        std::string file;          // .ptc, .pctp ou .pp
        std::string packName;      // nome do pack (vazio se nao vier de .pp)
        std::string packFile;      // arquivo .pp
        std::string script;        // arquivo .lua (pasta) ou entrada no pacote
        std::filesystem::path scriptPath;  // caminho no disco (plugins em pasta)
        std::shared_ptr<PluginPackage> package;
        std::string rootPrefix;    // pasta do plugin dentro do pacote
        std::string manifestEntry;
        std::string iconEntry;

        bool fromPack = false;
        bool isPackage = false;
        bool implicitManifest = false;
        mutable bool isAmp = false;

        mutable std::string status;  // "" = nao carregado, "ok" ou erro
        std::string error;           // erro de leitura do pacote/manifesto

        std::string sourceLabel() const {
            if (fromPack) return packName.empty() ? "pack" : packName;
            if (isPackage) return "package";
            return "folder";
        }
    };

    class PluginManager {
    public:
        explicit PluginManager(std::filesystem::path pluginsDir = "Plugins",
                               std::filesystem::path packsDir = "Packs")
            : dir_(std::move(pluginsDir)), packsDir_(std::move(packsDir)) {
            refresh();
        }

        void refresh();
        const std::vector<PluginInfo>& all() const { return plugins_; }
        const std::filesystem::path& dir() const { return dir_; }
        const std::filesystem::path& packsDir() const { return packsDir_; }

        // Importa .pctp, .pp, .ptc, .lua ou uma pasta inteira (vira .pp).
        // Retorna false e preenche `error` quando o arquivo nao e valido.
        bool importFile(const std::filesystem::path& source,
                        std::string& error,
                        std::filesystem::path* installed = nullptr);

        // Compacta uma pasta em .pp. O pack inclui pack.ini automaticamente.
        bool createPackFromFolder(const std::filesystem::path& folder,
                                  const std::filesystem::path& outFile,
                                  const std::string& name,
                                  const std::string& description,
                                  std::string& error,
                                  int* pluginCount = nullptr);

        // Remove o arquivo/pasta do plugin ou pack. Para plugins de pasta,
        // remove .ptc/.lua e a pasta de assets correspondente.
        bool removePlugin(const PluginInfo& info, std::string& error);
        bool removePack(const std::string& packFile, std::string& error);

    private:
        void scanFolderPlugins();
        void scanPackageFile(const std::filesystem::path& path, bool isPack);
        void addError(const std::string& name, const std::string& file,
                      const std::string& error, bool isPack);

        static std::filesystem::path uniquePath(const std::filesystem::path& wanted);
        static bool readFileText(const std::filesystem::path& path, std::string& out);
        static void copyAssetsFolder(const std::filesystem::path& fromDir,
                                     const std::filesystem::path& toDir,
                                     const std::string& stem);

        std::filesystem::path dir_;
        std::filesystem::path packsDir_;
        std::vector<PluginInfo> plugins_;
    };

}  // namespace gp
