# Changelog

## v0.1.0 — primeira versão pública

Primeira versão do port nativo do **ONRUSH (Xbox One 1.0.64980.34)** para Windows x64 com Direct3D 12. Nenhum arquivo do jogo vem junto: é preciso ter a sua própria cópia do pacote (veja o README).

### O que funciona
- O jogo abre e roda nativamente no PC: tela de título, EULA, menus, tutorial e corridas (testado no Countdown em Big Dune Beach), com controle XInput.
- Tradução em tempo real dos shaders do console (GCN → HLSL), gerados na sua máquina a partir da sua cópia do jogo.
- Cache de pipelines em disco: depois da primeira vez, as pistas carregam sem recompilar shaders.

### Gráficos
- Iluminação completa do G-buffer: sol, normais e relevo das superfícies, pintura dos carros.
- Luz ambiente e reflexos (cubemaps do ambiente e de irradiância).
- Céu e atmosfera pré-calculados: céu com gradiente, areia clara, água azul e sombras azuladas em vez de pretas.
- Árvores e terreno distantes voltam a aparecer (bypass da tesselação), sem buracos no horizonte ao pular rampas.
- Interface: fundo desfocado da EULA, degradês, barra de seleção, efeito VHS na câmera de batida.
- Abertura "PREPARAR / COUNTDOWN" com o carro renderizando.
- Corrigidas faixas pretas no chão, a grade de blocos coloridos em movimento e o "fantasma" verde nas rochas.

### Áudio
- Som do jogo (efeitos, música e vozes) pelo WASAPI.
- **Playlist própria**: toque suas músicas durante o jogo (a música original fica muda), com ordem aleatória e volume.

### Launcher
- **Prepara o jogo sozinho**: ao apertar JOGAR (ou ao escolher a pasta do jogo), basta apontar a sua cópia, ou seja, o pacote extraído com `Mount` e `EmbeddedXvd`. O launcher:
  - copia o executável e os arquivos pequenos (~60 MB) para a pasta dele;
  - liga `dat` e `EmbeddedXvd` como atalhos para a sua cópia;
  - cria o `onrush-pc.exe` (o jogo aparece como ONRUSH™ no Gerenciador de Tarefas).
  A sua cópia não é alterada. Também pela linha de comando: `-setup "<pasta da cópia>"`.
- **Shaders traduzidos sozinhos**, sem Python nem Windows SDK: o tradutor vem junto (`translator\`).
  - Na primeira vez, o JOGAR abre o jogo algumas vezes, em uma janela pequena e sem som, para ele gravar os shaders que usa; o launcher traduz tudo e só então abre o jogo de verdade.
  - Os shaders novos (pistas e menus que você ainda não tinha visitado) são traduzidos quando o jogo fecha, ou no próximo JOGAR.
  - Cerca de 120 shaders de computação levam de 3 s a 1 min cada para compilar. Eles são compilados primeiro no modo rápido, e a primeira preparação cai de mais de 10 minutos para poucos minutos. A versão otimizada é feita quando o jogo fecha, e dá para pular: continua na vez seguinte.
  - Usa no máximo 8 processos de compilação, limitado pela memória livre.
- Resolução, modo de tela (sem bordas, janela ou tela cheia) e VSync.
- Launcher em português, inglês ou espanhol; idioma do texto e da voz do jogo escolhidos separadamente.
- Opção de pular a introdução (tela legal e logos): título em ~2 s.
- Nome do jogador com save próprio e pasta de saves configurável.
- Modo foto: **F12** salva a imagem sem HUD em PNG ou JPG.
- Discord Rich Presence.
- Aba **Playlist**: criar, renomear e excluir playlists; arrastar músicas e pastas; reordenar; pesquisar; ouvir a prévia.
- "Preparando shaders" ao apertar JOGAR, com barra de progresso e botão para pular: tradução, compilação e cache de pipelines.
- Log embutido (ver, copiar, limpar), e o jogo abre sem janela de console.
- **Atualização automática**: o launcher avisa quando há uma versão nova aqui no GitHub. Se você aceitar, ele baixa o zip, confere o SHA-256, fecha, atualiza a própria pasta e abre de novo. Configurações, saves e shaders são mantidos.

### Estabilidade
- Corrigidos travamentos da GPU por despachos e draws indiretos inválidos (partículas).
- Corrigidos o fechamento ao apertar A na intro, a falha ao carregar o tutorial e travamentos no carregamento e na escolha do veículo.

### Problemas conhecidos
- Fumaça aparece como retângulos com fundo preto, e há placas pretas no chão (inclusive no rastro do rush).
- Blocos pixelados em árvores, água e rochas, e água com reflexo arco-íris em alguns pontos.
- Personagens invisíveis na tela de customização (só aparecem cabelo/contornos).
- Portões verdes do modo Countdown invisíveis (só as setas aparecem).
- Nuvens, poeira na lente (lens dust) e o splash de água dos carros ainda não aparecem.
- Crash ocasional depois do fim de uma corrida.
- Online, multiplayer e cosméticos que dependem do servidor não estão disponíveis.
- Pistas novas podem aparecer com objetos faltando até o jogo ser fechado e os shaders delas serem traduzidos.
