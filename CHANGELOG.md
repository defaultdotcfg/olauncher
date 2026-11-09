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
- Resolução, modo de tela (sem bordas, janela ou tela cheia) e VSync.
- Launcher em português, inglês ou espanhol; idioma do texto e da voz do jogo escolhidos separadamente.
- Opção de pular a introdução (tela legal e logos): título em ~2 s.
- Nome do jogador com save próprio e pasta de saves configurável.
- Modo foto: **F12** salva a imagem sem HUD em PNG ou JPG.
- Discord Rich Presence.
- Aba **Playlist**: criar, renomear e excluir playlists; arrastar músicas e pastas; reordenar; pesquisar; ouvir a prévia.
- "Preparando shaders" ao apertar JOGAR, com barra de progresso e botão para pular.
- Log embutido (ver, copiar, limpar), e o jogo abre sem janela de console.
- **Verificar atualizações**: avisa quando há uma versão nova aqui no GitHub e abre a página da release.

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
- A primeira execução de cada pista grava shaders novos. Rode `prepare_shaders.ps1` depois de jogar para traduzi-los (os de tesselação precisam de uma segunda passada).
