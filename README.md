# Pedals the Capybara

Pedals the Capybara é um pedalboard/amp sim em tempo real para guitarra, escrito em C++ com Dear ImGui + GLFW/OpenGL, miniaudio e Lua.

## O que mudou nesta reformulação

- **Imagens em plugins**: cards renderizam texturas (`basetexture`, `knobtexture`, `ledtexture`, `footswitchtexture`, `labeltexture`) tanto em plugins de pasta quanto dentro de pacotes.
- **Amp na mesma cadeia**: amps não ficam mais em slot fixo separado; são pedais normais com `isAmp=true` e podem ser ligados/desligados e arrastados na mesma signal chain.
- **`.pctp` (plugin package)**: ZIP contendo manifesto, script Lua e assets. O app lê diretamente.
- **`.pp` (plugin pack)**: ZIP contendo vários plugins/pacotes. O app importa e lista por pack; também dá para criar um `.pp` a partir de uma pasta pela UI.
- **Editor melhorado**: importar imagens, testar com assets em memória, salvar plugin em pasta, exportar `.pctp`.
- **UX**: busca na biblioteca, abas All/Pedals/Amps/My Plugins/Packs, drag-and-drop de arquivos (`.pctp/.pp/.ptc/.lua/imagens`), toasts de status/erro, dialogs nativos.
- **Exclusão**: na Library, selecione um plugin/pack e use **Delete... / Delete pack...** para remover do disco (com confirmação e checagem de caminho).
- **Packs incluídos**: demos em `Demos/` e packs prontos em `Packs/` (starter, BOSS inspired e rig Comfortably Numb).

## Formatos

### Plugin de pasta (compatível)
```text
Plugins/
  my_drive.ptc        # NAME, DESCRIPTION, AUTHOR, VERSION, SCRIPT, ICON
  my_drive.lua
  my_drive_assets/
    face.png
    knob.png
```

### `.pctp` (um plugin, ZIP)
```text
plugin.ptc
my_drive.lua
assets/
  face.png
  knob.png
```

### `.pp` (plugin pack, ZIP)
```text
pack.ini              # opcional: NAME/DESCRIPTION/AUTHOR/VERSION
MyDrive.pctp
AnotherPlugin/
  plugin.ptc
  script.lua
  assets/...
```

## Build (Windows)

```powershell
cmake -S . -B build
cmake --build build --config Release --parallel 8
.\build\Release\PedalTheCapybara.exe
```

Dependências são baixadas via FetchContent (GLFW, Dear ImGui, Lua, stb) e bibliotecas vendorizadas em `third_party/` (miniaudio, miniz, portable-file-dialogs).

## Packs incluídos

- `Packs/CapybaraStarter.pp` — pack de demonstração (Golden Echo, Velvet Chorus, Tape Warmth).
- `Packs/BossInspired.pp` — MD-2, DS-1, SD-1, BD-2 e MT-2 inspirados nos controles documentados pela BOSS. Modelos DSP originais; não são clones de circuito nem afiliados à BOSS/Roland.
- `Packs/ComfortablyNumbRig.pp` — rig de solo inspirado (comp, sustain/fuzz, modulação, delay e amp). Modelos DSP originais; não afiliado a Pink Floyd/BOSS/EHX/Hiwatt.
- `Packs/ComfortablyNumbSolo.pp` — alias do rig acima para importação rápida.

Os packs também podem ser importados arrastando o `.pp` para a janela ou usando **Import .pctp / .pp...** na Library.

## Criar plugin pack

1. Crie uma pasta com seus plugins (`.pctp`, ou pastas com `.ptc/.lua/assets`).
2. No app: **Plugins > Create plugin pack (.pp)...** ou botão **Create pack...** na Library.
3. Escolha a pasta e o destino `.pp`.

## Excluir plugins/packs

Selecione o item na Library e clique em **Delete...** (plugin de pasta/pacote avulso) ou **Delete pack...** (item vindo de um `.pp`). A remoção apaga os arquivos do disco; plugins que já estão na cadeia continuam ativos até serem removidos da cadeia.

## Documentação

- [Lua API Wiki](docs/wiki/Home.md)
