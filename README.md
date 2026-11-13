<p align="center">
  <img width="300" height="80" alt="rushlauncher" src="https://github.com/user-attachments/assets/60723459-9d94-48f2-b413-8f5a5d927254" />
</p>

# rushlauncher — Port para PC de ONRUSH™

Launcher e camada de tradução que rodam o **ONRUSH (Xbox One, versão final 1.80.100.100)** nativamente no Windows x64 com Direct3D 12.

> **Este repositório não contém nenhum arquivo do jogo.** Nada de executáveis, DLLs do sistema do console, assets, shaders originais ou traduzidos. Para jogar, você precisa da **sua própria cópia** do pacote do jogo. Os shaders traduzidos são gerados na sua máquina a partir dessa cópia.

## O que tem aqui

| Pasta | Conteúdo |
|---|---|
| `src/launcher` | `OnRushLauncher.exe`: prepara o jogo a partir da sua cópia, traduz os shaders, perfis, fotos, configurações, Discord Rich Presence e **atualização automática** |
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

## Como jogar

1. Extraia o zip da [última release](https://github.com/defaultdotcfg/rushlauncher/releases/latest) numa pasta (num disco NTFS).
2. Abra `OnRushLauncher.exe` e aperte **JOGAR**. O launcher pede a pasta da sua cópia do jogo: o pacote extraído, com `Mount\` e `EmbeddedXvd\`.
3. O launcher prepara o jogo na própria pasta:
   - copia o executável e os arquivos pequenos do jogo (~60 MB);
   - liga `dat\` e `EmbeddedXvd\` como atalhos (junctions) para a sua cópia, sem duplicar nada;
   - cria o `onrush-pc.exe`.
   A sua cópia não é alterada. Pela linha de comando: `OnRushLauncher.exe -setup "D:\caminho\do\pacote"`.
4. **Shaders**: na primeira vez, o JOGAR mostra "Preparando shaders" com uma barra só. O launcher lê os shaders direto dos pacotes do jogo (`dat\`), traduz e compila tudo de uma vez (tradutor embutido em `translator\`, sem Python) e só então abre o jogo. O jogo não é aberto para isso. Leva alguns minutos e acontece uma vez só (de novo apenas se os pacotes do jogo mudarem).
5. O botão **Pular** do "Preparando shaders" abre o jogo na hora; o que faltou é feito no próximo JOGAR.
6. O launcher procura novas versões no GitHub e, se você aceitar, se atualiza sozinho (configurações, saves, shaders e arquivos do jogo são mantidos).

## Como compilar

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1             # gera bin\
powershell -ExecutionPolicy Bypass -File scripts\build_translator.ps1  # gera bin\translator\ (Python + pyinstaller)
# opcional, sem o launcher: monta dist_game\ a partir da sua cópia
powershell -ExecutionPolicy Bypass -File scripts\stage.ps1 -GameRoot "D:\caminho\ONRUSH_1.0.64980.34_x64__62ktj6msykznt"
```

Para traduzir os shaders sem o launcher (com Python e o Windows SDK): `python scripts\translator\translate_all.py <pasta do jogo>`.

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

## Notas

O port está sendo desenvolvido e testado neste hardware:

- Ryzen 7 5700
- NVIDIA RTX 3060 (12GB)
- 40 GB RAM (DDR4)

Espere melhores otimizações e ajustes na compilação de shaders nas próximas versões.
