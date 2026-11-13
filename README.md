<p align="center">
  <img width="300" height="80" alt="RushLauncher" src="https://github.com/user-attachments/assets/60723459-9d94-48f2-b413-8f5a5d927254" />
</p>

<h2 align="center">RushLauncher — Port para PC de ONRUSH™</h2>

<p align="center">
  <b>Port não oficial para PC do ONRUSH™ (Xbox One, versão final 1.80.100.100).</b><br>
  Roda o jogo nativamente no Windows x64 com Direct3D 12, a partir da sua própria cópia.
</p>

<p align="center">
  <a href="https://github.com/defaultdotcfg/rushlauncher/releases/latest">Download</a> ·
  <a href="#início-rápido">Início rápido</a> ·
  <a href="CHANGELOG.md">Novidades</a> ·
  <a href="https://github.com/defaultdotcfg/rushlauncher/issues">Reportar um bug</a>
</p>

> [!IMPORTANT]
> O RushLauncher é um projeto de fãs, **não oficial**, sem nenhuma ligação com a Codemasters ou a Electronic Arts. **Este repositório e as releases não contêm nenhum arquivo do jogo**: nada de executáveis, DLLs do sistema do console, assets ou shaders (originais ou traduzidos). Para jogar, você precisa da **sua própria cópia** do pacote do jogo. Os shaders são lidos e traduzidos na sua máquina, a partir dessa cópia.

**Conteúdo:** [O que é](#o-que-é-o-rushlauncher) · [Destaques](#destaques) · [Download](#download) · [Início rápido](#início-rápido) · [Controles](#controles) · [Configurações](#configurações) · [Dicas](#dicas) · [Perguntas frequentes](#perguntas-frequentes-e-problemas) · [Próximos passos](#próximos-passos) · [Para desenvolvedores](#para-desenvolvedores) · [Créditos](#créditos) · [Licença](#licença) · [Aviso legal](#aviso-legal)

## O que é o RushLauncher?

O ONRUSH saiu para Xbox One e PlayStation 4, mas nunca para PC. O RushLauncher roda o executável de Xbox One do jogo direto no Windows, sem emulador. Ele substitui as bibliotecas do sistema do console por versões próprias e traduz para o Direct3D 12 do PC as chamadas gráficas e os shaders da GPU do Xbox (GCN).

O launcher cuida de tudo: prepara o jogo a partir da sua cópia, aplica o patch da versão final, traduz os shaders e abre o jogo. Veja [Como funciona](#como-funciona).

## Destaques

- **Nativo no Windows.** O código do jogo roda direto na CPU. Os gráficos usam Direct3D 12.
- **Shaders preparados uma vez.** Na primeira vez, o launcher lê os shaders direto dos pacotes do jogo, traduz e compila tudo de uma vez, com uma barra de progresso. O jogo não precisa ser aberto para isso.
- **Sem duplicar o jogo.** Os pacotes grandes (`dat\`, ~34 GB) ficam na sua cópia e são ligados por atalhos (junctions). Só ~60 MB são copiados.
- **Versão final do jogo.** Se a sua cópia for a 1.0.64980.34, o launcher aplica o patch 1.80.100.100 sozinho.
- **Perfis.** Vários jogadores, cada um com o próprio save e as próprias horas de jogo.
- **Modo foto.** **F12** salva a imagem do jogo sem a interface, em PNG ou JPG. A aba **Fotos** mostra a galeria.
- **Atualização automática.** O launcher procura novas versões no GitHub e se atualiza sozinho, mantendo configurações, saves e shaders.
- **Discord Rich Presence**, idiomas do texto e da voz do jogo, e launcher em português, inglês e espanhol.

## Download

| Plataforma | Como obter | Testado em |
|---|---|---|
| Windows 10/11 x64 | Zip `rushlauncher-vX.Y.Z.zip` da [última release](https://github.com/defaultdotcfg/rushlauncher/releases/latest) | Windows 11, Ryzen 7 5700, RTX 3060 12 GB |

Para conferir o zip baixado, compare o SHA-256 com o que aparece ao lado do arquivo na página da release:

```powershell
Get-FileHash .\rushlauncher-vX.Y.Z.zip -Algorithm SHA256
```

O que mudou em cada versão está no [CHANGELOG](CHANGELOG.md).

### Requisitos do sistema

- Windows 10 ou 11, 64 bits.
- GPU com Direct3D 12 (feature level 12_0) e driver atualizado.
- Disco NTFS (os atalhos `dat\` e `EmbeddedXvd\` são junctions).
- A sua cópia do jogo: o pacote extraído do ONRUSH para Xbox One (a pasta com `Mount\` e `EmbeddedXvd\`). Se ela for a versão 1.0.64980.34, também a pasta do patch da versão 1.80.100.100.
- Espaço: ~60 MB para o jogo preparado, mais ~1 GB para os shaders traduzidos.

## Início rápido

1. Extraia o zip inteiro numa pasta, num disco NTFS.
2. Abra o `OnRushLauncher.exe` e escolha a pasta da sua cópia do jogo (o pacote extraído, com `Mount\` e `EmbeddedXvd\`).
3. Se a cópia não estiver na versão final, o launcher pede a pasta do patch 1.80.100.100 e aplica as diferenças. Os pacotes novos são **movidos** da pasta do patch para a cópia: no mesmo disco, isso é instantâneo e não ocupa espaço extra.
4. O launcher prepara o jogo na própria pasta: copia o executável e os arquivos pequenos, liga `dat\` e `EmbeddedXvd\` à sua cópia e cria o `onrush-pc.exe`. A sua cópia não é alterada.
5. Aperte **JOGAR**. Na primeira vez aparece **Preparando shaders**: o launcher lê os shaders dos pacotes do jogo, traduz e compila tudo, e só então abre o jogo. Leva alguns minutos (cerca de 3 minutos numa CPU de 8 núcleos e 16 threads, mais a leitura dos pacotes) e acontece uma vez só. Ela só se repete se os pacotes do jogo mudarem ou se uma versão nova do tradutor precisar.
6. O botão **Pular** abre o jogo na hora. O que faltou é feito no próximo JOGAR.

Pela linha de comando, `OnRushLauncher.exe -setup "D:\caminho\do\pacote"` prepara o jogo sem passar pela interface.

### Como atualizar

O launcher procura novas versões ao abrir (e no botão **Verificar atualizações**). Se você aceitar, ele baixa o zip, confere o SHA-256, fecha, instala e abre de novo. Configurações, perfis, saves, fotos, shaders e os arquivos do jogo são mantidos. Para atualizar à mão, extraia o zip novo por cima da pasta antiga.

## Controles

Controles de Xbox (XInput) funcionam direto. O teclado vira um controle virtual, e só é lido enquanto a janela do jogo está em foco:

| Controle | Teclado |
|---|---|
| A | Enter ou Espaço |
| B | Esc ou Backspace |
| X / Y | X / Y |
| Acelerar (RT) | W ou ↑ |
| Frear e ré (LT) | S ou ↓ |
| Direção (analógico esquerdo e direcional) | W A S D ou setas |
| Analógico direito | I J K L |
| LB / RB | Q / E |
| View | Tab |
| Menu | F1 |

| Atalho | Ação |
|---|---|
| **F12** | Foto sem interface (pasta `fotos\`) |
| **F11** | Salva os próximos 12 quadros como BMP (útil para reportar falhas de imagem) |
| **Alt+Enter** | Alterna entre janela e tela cheia sem bordas |
| **Alt+F4** | Fecha o jogo |

## Configurações

<details>
<summary>Opções do launcher (aba <b>Jogar</b>)</summary>

| Opção | Escolhas | Padrão |
|---|---|---|
| Resolução | Resoluções do monitor | Resolução do monitor |
| Modo de tela | Sem bordas, Janela, Tela cheia | Sem bordas |
| Sincronização vertical (VSync) | Ligada ou desligada | Ligada |
| Idioma do texto | Automático (sistema) ou um idioma do jogo | Automático |
| Idioma da voz | Igual ao texto ou um idioma do jogo | Igual ao texto |
| Pular introdução | Pula a tela legal e os logos | Desligado |
| Fotos do modo foto (F12) | PNG (maior qualidade) ou JPG (mais leve) | PNG |
| Discord Rich Presence | Mostra que você está jogando ONRUSH | Desligado |
| Pasta do jogo / Pasta de saves | Qualquer pasta | Pasta do launcher |
| Idioma do launcher | Português, English, Español | Português |

| Opção avançada | Para que serve |
|---|---|
| Camada de depuração do Direct3D 12 | Diagnóstico de erros gráficos (deixa o jogo mais lento) |
| Modo de depuração | Log completo e capturas BMP de quadros, para reportar problemas |

A aba **Perfis** cria, renomeia e exclui perfis (o nome do perfil é o nome do jogador). Cada perfil tem dois interruptores, aplicados só enquanto o jogo roda (o save não é alterado): **Desbloquear cosméticos** e **Desbloquear tudo (cosméticos e eventos)**.

</details>

## Dicas

- Deixe a primeira preparação de shaders terminar. Com **Pular**, o jogo abre antes, mas pode faltar parte da imagem até o próximo JOGAR.
- Guarde a sua cópia do jogo no mesmo disco do launcher: os atalhos e o patch ficam instantâneos.
- Depois de atualizar o driver de vídeo, o primeiro JOGAR remonta o cache de pipelines (alguns segundos).
- A tela cheia sem bordas é a mais estável. Use **Alt+Enter** para trocar sem fechar o jogo.

## Perguntas frequentes e problemas

**O jogo não vem junto?** Não. Você precisa da sua própria cópia do pacote de Xbox One. O projeto não distribui nada do jogo.

**Funciona com a versão 1.0.64980.34?** Funciona depois do patch 1.80.100.100, que o launcher aplica sozinho se você indicar a pasta do patch.

**A preparação de shaders vai rodar toda vez?** Não. Ela roda na primeira vez, quando os pacotes do jogo mudam ou quando uma versão nova do launcher traz um tradutor corrigido. Nos outros casos, o JOGAR abre o jogo em cerca de 1 segundo.

**Partes da imagem estão erradas ou faltando.** Alguns efeitos ainda não são traduzidos corretamente (veja [Próximos passos](#próximos-passos)). Reporte com uma captura: **F11** salva 12 quadros seguidos.

| O quê | Onde fica (pasta do launcher) |
|---|---|
| Log do launcher | `onrush_launcher.log` |
| Log do jogo e da ponte gráfica | `onrush_d3d12_x.log` (a sessão anterior fica em `*.prev.log`) |
| Configurações | `onrush.ini` |
| Saves | `save\<jogador>\` (ou a pasta de saves escolhida) |
| Fotos | `fotos\` |
| Shaders traduzidos | `shaders\translated\` |

### Reportando um bug

1. Ligue o **Modo de depuração** nas opções avançadas e reproduza o problema.
2. Se for um problema de imagem, aperte **F11** na hora.
3. Abra uma [issue](https://github.com/defaultdotcfg/rushlauncher/issues) com o que aconteceu, os logs `onrush_launcher.log` e `onrush_d3d12_x.log`, as capturas `frame_*_backbuffer.bmp` e o modelo da sua GPU.

## Próximos passos

- Corrigir os efeitos visuais que ainda faltam ou aparecem errados (partículas, aura do Rush, efeitos de água).
- Otimizar o desempenho e a preparação de shaders.

Sem datas: as versões saem quando estiverem prontas.

## Para desenvolvedores

### Como funciona

| Pasta | Conteúdo |
|---|---|
| `src/launcher` | `OnRushLauncher.exe`: prepara o jogo, aplica o patch, extrai e traduz os shaders, perfis, fotos, configurações, Discord e atualização automática |
| `src/d3d12_x` | Ponte D3D12.X (Xbox) → D3D12 de PC: PSOs, descritores, tradução de shaders, ESRAM, modo foto, música |
| `src/kernelx`, `src/combase`, `src/EtwPlus`, `src/MMDevAPI` | Substitutos das bibliotecas do sistema do console |
| `scripts/translator` | Tradutor GCN → HLSL (`gcn2hlsl.py`), bypass de tesselação e compilação com o DXC; `translate_all.py` é o ponto de entrada |
| `scripts/build_translator.ps1` | Empacota o tradutor como `translator\olauncher-translator.exe` (PyInstaller) com o DXC do Windows SDK |
| `scripts/build.ps1` | Compila tudo em `bin\` |
| `scripts/stage.ps1` | Monta a pasta jogável juntando a sua cópia do jogo com os binários |

A preparação de shaders tem três partes:
1. **Extração.** O launcher lê os pacotes `dat\*.dat` (fluxos LZ4) e grava cada shader (VS, PS, CS e pares de tesselação) em `shaders\`, com o mesmo nome que a ponte usa.
2. **Tradução.** O tradutor converte o microcódigo GCN de cada shader em HLSL e compila para DXIL com o DXC, em paralelo.
3. **Pipelines.** O launcher monta o cache de pipelines da sua GPU.

### Compilando

Requisitos: Visual Studio 2022 ou mais novo, com **Desenvolvimento para desktop com C++**; Windows SDK 10.0.22621 ou mais novo; e, para empacotar o tradutor, **Python 3.10+** com `pyinstaller`.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1             # gera bin\
powershell -ExecutionPolicy Bypass -File scripts\build_translator.ps1  # gera bin\translator\ (Python + pyinstaller)
# opcional, sem o launcher: monta dist_game\ a partir da sua cópia
powershell -ExecutionPolicy Bypass -File scripts\stage.ps1 -GameRoot "D:\caminho\do\pacote"
```

Para traduzir os shaders sem o launcher (com Python e o Windows SDK): `python scripts\translator\translate_all.py <pasta do jogo>`.

### Variáveis de diagnóstico

| Variável | Efeito |
|---|---|
| `ONRUSH_SKIP_INTRO=1` | Pula a tela legal e os logos |
| `ONRUSH_NO_AUDIO=1` | Roda sem áudio |
| `ONRUSH_VERBOSE_LOG=1` | Log completo (sem limite de linhas por mensagem) |
| `ONRUSH_FRAME_DUMPS=1` | Capturas periódicas `frame_*_backbuffer.bmp` |
| `ONRUSH_NO_TESS_BYPASS=1` | Desliga o bypass de tesselação |
| `ONRUSH_SKIP_DRAW_PS=h1,h2` | Pula draws pelo hash do PS/VS |
| `HLSLC_DXC_EXE=1` | O tradutor usa o `dxc.exe` em vez do DXC dentro do processo |

## Créditos

| Projeto | Para que é usado | Licença |
|---|---|---|
| [WinDurango](https://github.com/WinDurango/WinDurango) | Base de `src/d3d12_x` e `src/MMDevAPI` | LGPL-3.0 |
| SlimEra (XWine1) | Base de `src/kernelx`, `src/combase` e `src/EtwPlus` | MIT |
| [DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler) | Compila os shaders traduzidos (vai no zip, em `translator\`) | NCSA + MIT; `dxil.dll` sob a licença da Microsoft que acompanha o DXC |
| [Python](https://www.python.org) e [PyInstaller](https://pyinstaller.org) | Executam o tradutor empacotado | PSF; bootloader GPL com exceção |

Os textos das licenças estão em `licenses\` e, no zip, em `translator\licenses\`. Detalhes em [THIRD_PARTY.md](THIRD_PARTY.md).

## Licença

Distribuído sob a **GPL-3.0** (`LICENSE`), compatível com o código de terceiros acima.

## Aviso legal

ONRUSH é marca registrada da Codemasters e da Electronic Arts. Xbox e Direct3D são marcas da Microsoft. Este projeto não é afiliado, patrocinado nem endossado por nenhuma delas. Nenhum arquivo do jogo ou do software do sistema do Xbox One está incluído: o código-fonte contém só o necessário para interoperar com eles (layouts de funções e estruturas, mapeamentos e hashes de shaders).

## Notas

O port está sendo desenvolvido e testado com:

- Ryzen 7 5700
- NVIDIA RTX 3060 (12 GB)
- 40 GB de RAM (DDR4)

Espere melhores otimizações e ajustes na compilação de shaders nas próximas versões.
