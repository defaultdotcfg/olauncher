# olauncher — ONRUSH PC Native Port

Launcher e camada de tradução que rodam o **ONRUSH (Xbox One, versão 1.0.64980.34)** nativamente no Windows x64 com Direct3D 12.

> **Este repositório não contém nenhum arquivo do jogo.** Nada de executáveis, DLLs do sistema do console, assets, shaders originais ou traduzidos. Para jogar, você precisa da **sua própria cópia** do pacote do jogo. Os shaders traduzidos são gerados na sua máquina a partir dessa cópia.

*English summary below.*

## O que tem aqui

| Pasta | Conteúdo |
|---|---|
| `src/launcher` | `OnRushLauncher.exe`: configurações, playlist, Discord Rich Presence, preparo de shaders e **verificação de atualizações** |
| `src/d3d12_x` | Ponte D3D12.X (Xbox) → D3D12 de PC: PSOs, descritores, tradução de shaders, ESRAM, modo foto, música |
| `src/kernelx`, `src/combase`, `src/EtwPlus`, `src/MMDevAPI` | Substitutos das bibliotecas do sistema do console |
| `scripts/translator` | Tradutor GCN → HLSL (`gcn2hlsl.py`), bypass de tesselação e compilação dos `.cso` |
| `scripts/build.ps1` | Compila tudo em `bin\` |
| `scripts/stage.ps1` | Monta a pasta jogável juntando a sua cópia do jogo com os binários |

## Requisitos

- Windows 10/11 x64, GPU com Direct3D 12 (feature level 12_0)
- Para compilar: Visual Studio 2022 ou mais novo com **Desenvolvimento para desktop com C++** e Windows SDK 10.0.22621 ou mais novo
- Para gerar os shaders: **Python 3.10+** no PATH (o `dxc.exe` vem do Windows SDK; também dá para indicar com `$env:DXC`)
- O pacote extraído do jogo (a pasta com `Mount\` e `EmbeddedXvd\`)

## Como jogar (zip da release)

1. Extraia o zip da [última release](https://github.com/defaultdotcfg/olauncher/releases/latest) numa pasta (num disco NTFS).
2. Abra `OnRushLauncher.exe` e aperte **JOGAR**. O launcher pede a pasta da sua cópia do jogo: o pacote extraído, com `Mount\` e `EmbeddedXvd\`.
3. O launcher prepara o jogo na própria pasta:
   - copia o executável e os arquivos pequenos do jogo (~60 MB);
   - liga `dat\` e `EmbeddedXvd\` como atalhos (junctions) para a sua cópia, sem duplicar nada;
   - cria o `onrush-pc.exe`.
   A sua cópia não é alterada. Pela linha de comando: `OnRushLauncher.exe -setup "D:\caminho\do\pacote"`.
4. **Shaders**: na primeira execução a tela fica preta, porque os shaders do console ainda não foram traduzidos. O jogo grava em `shaders\` os shaders que usa; depois disso, traduza-os (precisa de Python 3.10+ e do Windows SDK):

```powershell
powershell -ExecutionPolicy Bypass -File scripts\translator\prepare_shaders.ps1 -GameDir .
```

Repita o passo 4 depois de visitar menus e pistas novas: cada execução grava os shaders que ainda faltam. Os shaders de tesselação (árvores e terreno distantes) precisam de uma segunda passada. Ao apertar **JOGAR**, o launcher compila os `.hlsl` novos e reconstrói o cache de pipelines sozinho.

## Como compilar

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1      # gera bin\
# opcional, sem o launcher: monta dist_game\ a partir da sua cópia
powershell -ExecutionPolicy Bypass -File scripts\stage.ps1 -GameRoot "D:\caminho\ONRUSH_1.0.64980.34_x64__62ktj6msykznt"
```

## Atualizações do launcher

O launcher consulta `https://api.github.com/repos/defaultdotcfg/olauncher/releases/latest` ao abrir (em silêncio) e quando você clica em **Verificar atualizações**, no canto inferior da arte.

- Se a tag da última release (`vX.Y.Z`) for maior que a versão em `src/launcher/version.h`, o botão fica laranja e oferece abrir a página da release.
- Nada é baixado ou instalado automaticamente.
- A verificação ao abrir pode ser desligada com `OnRushLauncher.exe -noupdatecheck`.

### Publicar uma versão

1. Atualize `OLAUNCHER_VERSION` em `src/launcher/version.h` (ex.: `0.2.0`) e adicione a seção `## v0.2.0` no `CHANGELOG.md`. Esse texto vira a descrição da release.
2. Faça o commit e crie a tag igual: `git tag v0.2.0 && git push origin v0.2.0`.
3. O workflow `.github/workflows/release.yml` compila, confere se a tag bate com `version.h` e publica a release com o zip `olauncher-v0.2.0.zip`. O zip traz os binários, os scripts e as licenças, sem nenhum arquivo do jogo.

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

## Histórico

Texto original do repositório, do começo do projeto (antes do primeiro render):

A WIP OnRush PC port for Windows

This is just a repository. The port is on earlier stages and I don't have any prediction of a playable state yet. 
For now, I did what I tought it was impossible: get the first render.

The port is getting build using XWine, XDLCompiler and Windurango. Azimuth Launcher was used only for research.

Note: the entire project was built using Antigravity IDE, using Gemini 3.8 Flash model.

<img width="1919" height="1079" alt="ima1ge" src="https://github.com/user-attachments/assets/080503c2-f734-4f27-b633-1d1c3e73f683" />

---

## English summary

A native Windows x64 / Direct3D 12 port layer and launcher for **ONRUSH (Xbox One 1.0.64980.34)**. The repository contains **no game files**. You need your own copy of the game package; translated shaders are generated locally from the shaders the game dumps while running. Build with `scripts\build.ps1`, assemble with `scripts\stage.ps1 -GameRoot <package>`, play once, then run `scripts\translator\prepare_shaders.ps1 -GameDir dist_game`. The launcher checks this repository's latest GitHub release and offers to open its page when a newer `vX.Y.Z` tag exists. Licensed GPL-3.0 (see `THIRD_PARTY.md`).
