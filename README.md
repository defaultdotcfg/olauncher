# olauncher — Port para PC de ONRUSH™

Launcher e camada de tradução que rodam o **ONRUSH (Xbox One, versão 1.0.64980.34)** nativamente no Windows x64 com Direct3D 12.

> **Este repositório não contém nenhum arquivo do jogo.** Nada de executáveis, DLLs do sistema do console, assets, shaders originais ou traduzidos. Para jogar, você precisa da **sua própria cópia** do pacote do jogo. Os shaders traduzidos são gerados na sua máquina a partir dessa cópia.

*English summary below.*

## O que tem aqui

| Pasta | Conteúdo |
|---|---|
| `src/launcher` | `OnRushLauncher.exe`: prepara o jogo a partir da sua cópia, traduz os shaders, configurações, playlist, Discord Rich Presence e **atualização automática** |
| `src/d3d12_x` | Ponte D3D12.X (Xbox) → D3D12 de PC: PSOs, descritores, tradução de shaders, ESRAM, modo foto, música |
| `src/kernelx`, `src/combase`, `src/EtwPlus`, `src/MMDevAPI` | Substitutos das bibliotecas do sistema do console |
| `scripts/translator` | Tradutor GCN → HLSL (`gcn2hlsl.py`), bypass de tesselação e compilação dos `.cso`; `translate_all.py` é o ponto de entrada incremental |
| `scripts/build_translator.ps1` | Empacota o tradutor como `translator\olauncher-translator.exe` (PyInstaller) com o `dxc.exe` do Windows SDK |
| `scripts/build.ps1` | Compila tudo em `bin\` |
| `scripts/stage.ps1` | Monta a pasta jogável juntando a sua cópia do jogo com os binários |

## Requisitos

- Windows 10/11 x64, GPU com Direct3D 12 (feature level 12_0)
- O pacote extraído do jogo (a pasta com `Mount\` e `EmbeddedXvd\`)
- Só para compilar: Visual Studio 2022 ou mais novo com **Desenvolvimento para desktop com C++**, Windows SDK 10.0.22621 ou mais novo e, para empacotar o tradutor, **Python 3.10+** com `pyinstaller`

## Como jogar (zip da release)

1. Extraia o zip da [última release](https://github.com/defaultdotcfg/olauncher/releases/latest) numa pasta (num disco NTFS).
2. Abra `OnRushLauncher.exe` e aperte **JOGAR**. O launcher pede a pasta da sua cópia do jogo: o pacote extraído, com `Mount\` e `EmbeddedXvd\`.
3. O launcher prepara o jogo na própria pasta:
   - copia o executável e os arquivos pequenos do jogo (~60 MB);
   - liga `dat\` e `EmbeddedXvd\` como atalhos (junctions) para a sua cópia, sem duplicar nada;
   - cria o `onrush-pc.exe`.
   A sua cópia não é alterada. Pela linha de comando: `OnRushLauncher.exe -setup "D:\caminho\do\pacote"`.
4. **Shaders**: na primeira vez, o JOGAR mostra "Preparando shaders" e abre o jogo algumas vezes sozinho, numa janela pequena e sem som, para ele gravar os shaders que usa. O launcher traduz esses shaders (tradutor embutido em `translator\`, sem Python) e só então abre o jogo de verdade. Isso leva alguns minutos e acontece uma vez só.
5. Depois, os shaders de menus e pistas que você ainda não tinha visitado são traduzidos quando o jogo fecha (ou no próximo JOGAR). O botão **Pular** do "Preparando shaders" abre o jogo na hora; o que faltou é feito na próxima vez.

## Como compilar

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1             # gera bin\
powershell -ExecutionPolicy Bypass -File scripts\build_translator.ps1  # gera bin\translator\ (Python + pyinstaller)
# opcional, sem o launcher: monta dist_game\ a partir da sua cópia
powershell -ExecutionPolicy Bypass -File scripts\stage.ps1 -GameRoot "D:\caminho\ONRUSH_1.0.64980.34_x64__62ktj6msykznt"
```

Para traduzir os shaders sem o launcher (com Python e o Windows SDK): `python scripts\translator\translate_all.py <pasta do jogo>`.

## Atualizações do launcher

O launcher consulta `https://api.github.com/repos/defaultdotcfg/olauncher/releases/latest` ao abrir (em silêncio) e quando você clica em **Verificar atualizações**, no canto inferior da arte.

- Se a tag da última release (`vX.Y.Z`) for maior que a versão em `src/launcher/version.h`, o botão fica laranja. Ao clicar, o launcher pergunta se deve instalar.
- Se você aceitar, ele baixa o zip da release (só de `github.com/defaultdotcfg/olauncher/releases/download/`), confere o SHA-256 publicado pelo GitHub, fecha, copia os arquivos novos por cima da pasta dele e abre de novo. `onrush.ini`, `save\`, `shaders\`, playlists e os arquivos do jogo nunca são tocados.
- Nada é instalado sem a sua confirmação, e a atualização fica bloqueada enquanto o jogo está aberto.
- A verificação ao abrir pode ser desligada com `OnRushLauncher.exe -noupdatecheck`.

### Publicar uma versão

1. Atualize `OLAUNCHER_VERSION` em `src/launcher/version.h` (ex.: `0.2.0`) e adicione a seção `## v0.2.0` no `CHANGELOG.md`. Esse texto vira a descrição da release.
2. Faça o commit e crie a tag igual: `git tag v0.2.0 && git push origin v0.2.0`.
3. O workflow `.github/workflows/release.yml` compila, empacota o tradutor, confere se a tag bate com `version.h` e publica a release com o zip `olauncher-v0.2.0.zip`. O zip traz os binários, o tradutor, os scripts e as licenças, sem nenhum arquivo do jogo.

## Variáveis de diagnóstico (opcional)

| Variável | Efeito |
|---|---|
| `ONRUSH_SKIP_INTRO=1` | pula a tela legal e os logos |
| `ONRUSH_NO_AUDIO=1` | sem áudio |
| `ONRUSH_NO_TESS_BYPASS=1` | desliga o bypass de tesselação |
| `ONRUSH_TESS_MAGENTA=1` | pinta de magenta o que passa pelo bypass (precisa de `shaders\translated\tess_magenta.dxil.cso`) |
| `ONRUSH_SKIP_DRAW_PS=h1,h2` | pula draws pelo hash do PS/VS |

## Licença

`src/d3d12_x` deriva do [WinDurango](https://github.com/WinDurango/WinDurango) (LGPL-3.0), e `src/kernelx`, `src/combase` e `src/EtwPlus` derivam do SlimEra da XWine1 (MIT). O projeto é distribuído sob **GPL-3.0** (`LICENSE`), compatível com as duas. Veja `THIRD_PARTY.md`.

ONRUSH é marca da Codemasters/Electronic Arts. Este projeto não é afiliado nem endossado por elas e não distribui conteúdo do jogo.

---

## English summary

A native Windows x64 / Direct3D 12 port layer and launcher for **ONRUSH (Xbox One 1.0.64980.34)**. The repository contains **no game files**. You need your own copy of the game package; translated shaders are generated locally from the shaders the game dumps while running. Extract the release zip, run `OnRushLauncher.exe` and press JOGAR: it sets the game up from your copy (copies the small files, links the big folders, creates `onrush-pc.exe`), runs the game a few times on its own to record its shaders, translates them with the bundled translator (no Python needed) and then starts the game. Shaders recorded later are translated when the game closes. The launcher checks this repository's latest GitHub release and, if you accept, downloads it, verifies its SHA-256, and updates its own folder and restarts (settings, saves, shaders and game files are kept). To build: `scripts\build.ps1` and `scripts\build_translator.ps1`. Licensed GPL-3.0 (see `THIRD_PARTY.md`).
