// Smoke test: abre os .pctp/.pp incluidos no repositorio e valida que
// o manifesto, o script Lua e os assets sao encontrados. Nao precisa de
// OpenGL nem de audio -- so da biblioteca de pacotes.
#include "plugin_package.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace gp;

namespace {

bool report_error(const fs::path& path, const std::string& what, const std::string& error)
{
    std::fprintf(stderr, "FAIL %s: %s%s%s\n", path.string().c_str(), what.c_str(),
                 error.empty() ? "" : " -- ", error.c_str());
    return false;
}

bool check_package(const fs::path& path, bool isPack)
{
    std::string error;
    auto package = PluginPackage::openFile(path, error);
    if (!package || !package->ok()) return report_error(path, "nao abriu", error);

    const auto discovery = discoverPlugins(package, isPack);
    if (!discovery.error.empty()) return report_error(path, "descoberta falhou", discovery.error);
    if (discovery.plugins.empty()) return report_error(path, "nenhum plugin encontrado", {});

    int scripts = 0;
    for (const auto& candidate : discovery.plugins) {
        if (!candidate.error.empty()) return report_error(path, "plugin com erro", candidate.error);
        if (candidate.scriptEntry.empty()) return report_error(path, "script nao encontrado", {});

        // Um .pp pode conter .pctp internos: nesse caso o script vive no
        // pacote interno, nao no arquivo que abrimos.
        std::string code;
        if (!candidate.package->readText(candidate.scriptEntry, code, error))
            return report_error(path, "nao leu " + candidate.scriptEntry, error);
        if (code.empty()) return report_error(path, "script vazio: " + candidate.scriptEntry, {});
        ++scripts;
    }

    const auto images = package->imageEntries();
    std::printf("ok   %s (%zu entradas, %d plugin(s), %zu imagem(ns))\n",
                path.filename().string().c_str(), package->entries().size(), scripts, images.size());
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "uso: smoke_packages <arquivo.pctp> [arquivo.pp ...]\n");
        return 2;
    }

    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        const fs::path path(argv[i]);
        const bool isPack = lowerAscii(path.extension().string()) == ".pp";
        if (!check_package(path, isPack)) ++failures;
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d pacote(s) falharam\n", failures);
        return 1;
    }

    std::printf("todos os pacotes estao validos\n");
    return 0;
}
