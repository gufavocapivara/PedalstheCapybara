// plugin_package.h - Leitura e criacao de pacotes de plugins.
//
// .pctp = um plugin em um ZIP (manifesto + script + imagens).
// .pp   = um "plugin pack" em um ZIP com varios plugins (pastas ou .pctp).
//
// O app nunca extrai arquivos para o disco: tudo e lido da memoria, entao um
// pacote malicioso nao consegue escrever fora da pasta de plugins.
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gp {

    // ------------------------------------------------------------------ manifesto
    struct PackageManifest {
        std::string name;
        std::string description;
        std::string author;
        std::string version;
        std::string script;   // caminho do .lua dentro do pacote
        std::string icon;     // caminho de uma imagem de icone
        bool empty() const { return name.empty() && script.empty() && description.empty(); }
    };

    PackageManifest parseManifest(const std::string& text);

    // ------------------------------------------------------------------ entradas
    struct PackageEntry {
        std::string name;      // caminho normalizado, ex.: "assets/face.png"
        std::uint64_t size = 0;
        std::uint32_t index = 0;
        bool isDir = false;
    };

    // ------------------------------------------------------------------ pacote
    class PluginPackage {
    public:
        ~PluginPackage();

        PluginPackage(const PluginPackage&) = delete;
        PluginPackage& operator=(const PluginPackage&) = delete;

        // Abre um .pctp/.pp do disco. O arquivo inteiro e carregado na memoria
        // para que o pacote continue valido mesmo se o arquivo for movido.
        static std::shared_ptr<PluginPackage> openFile(const std::filesystem::path& path, std::string& error);
        static std::shared_ptr<PluginPackage> openMemory(std::vector<unsigned char> bytes,
                                                         std::string displayName,
                                                         std::string& error);

        const std::filesystem::path& path() const;
        const std::string& displayName() const;
        const std::vector<PackageEntry>& entries() const;
        bool ok() const;

        bool hasEntry(const std::string& name) const;
        const PackageEntry* findEntry(const std::string& name) const;

        // Le uma entrada com limites de seguranca. `error` recebe o motivo.
        bool readEntry(const std::string& name, std::vector<unsigned char>& out, std::string& error) const;
        bool readText(const std::string& name, std::string& out, std::string& error) const;

        // Procura uma imagem usada por loadassets()/basetexture("...").
        // Testa o caminho exato, a pasta do modulo, <script>_assets, assets/
        // e, por fim, o nome do arquivo em qualquer subpasta do plugin.
        const PackageEntry* findAsset(const std::string& name,
                                      const std::string& module,
                                      const std::string& rootPrefix,
                                      const std::string& scriptStem) const;

        std::vector<std::string> imageEntries(const std::string& rootPrefix = {}) const;

    private:
        PluginPackage();
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    // ------------------------------------------------------------------ escrita
    struct PackageFile {
        std::string name;                    // caminho dentro do ZIP
        std::vector<unsigned char> data;
    };

    // Cria um ZIP com os arquivos informados (usado pelo .pctp do editor).
    bool writePackage(const std::filesystem::path& outFile,
                      const std::vector<PackageFile>& files,
                      std::string& error);

    // Compacta uma pasta inteira em um .pp. Tambem gera o pack.ini.
    bool writePackFromDirectory(const std::filesystem::path& folder,
                                const std::filesystem::path& outFile,
                                const std::string& packName,
                                const std::string& description,
                                std::string& error,
                                int* pluginCount = nullptr);

    // Conta quantos plugins existem em uma pasta (para validar antes de compactar).
    int countPluginsInFolder(const std::filesystem::path& folder);

    // ------------------------------------------------------------------ descoberta
    struct PluginCandidate {
        std::shared_ptr<PluginPackage> package;  // pacote que contem o plugin
        std::string rootPrefix;                  // pasta do plugin dentro do pacote ("" = raiz)
        std::string manifestEntry;               // entrada do manifesto (.ptc) ou ""
        std::string scriptEntry;                 // entrada completa do .lua
        PackageManifest manifest;
        bool implicitManifest = false;           // plugin sem .ptc (so pasta + .lua)
        std::string error;                       // erro de leitura desse candidato
    };

    struct PackDiscovery {
        PackageManifest manifest;                // pack.ini (se existir)
        std::vector<PluginCandidate> plugins;
        std::string error;
    };

    // Descobre os plugins dentro de um pacote. `isPack` deve ser true para .pp.
    // Para .pctp use false; se o arquivo na verdade for um pack, ele ainda e
    // detectado automaticamente.
    PackDiscovery discoverPlugins(std::shared_ptr<PluginPackage> package, bool isPack);

    // ------------------------------------------------------------------ utilidades
    bool isImageFile(const std::string& name);
    bool isLuaFile(const std::string& name);
    bool isPackageFile(const std::string& name);   // .pctp ou .pp
    bool isPackFile(const std::string& name);      // .pp
    std::string normalizeZipPath(const std::string& path);
    std::string pathBasename(const std::string& path);
    std::string pathDirname(const std::string& path);
    std::string pathStem(const std::string& path);
    std::string pathJoin(const std::string& a, const std::string& b);
    std::string lowerAscii(std::string s);

}  // namespace gp
