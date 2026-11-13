# Changelog

## v0.1.1 — versão final do jogo, perfis, fotos e correções gráficas

**Atenção:** a partir desta versão o port mira só a versão final do jogo, **1.80.100.100**. Se a sua cópia for a 1.0.64980.34, o launcher pede a pasta do patch de atualização e aplica as diferenças sozinho (os arquivos do patch são movidos para a pasta do jogo, não copiados).

### Launcher
- O projeto agora se chama **rushlauncher**: janela e mensagens com o nome novo, verificação de atualização no repositório `defaultdotcfg/rushlauncher`.
- Novo ícone no launcher e no `onrush-pc.exe` (o `onrush-pc.exe` de instalações antigas é recriado com o ícone no próximo JOGAR).
- Logo maior e centralizado no painel lateral, em todas as abas (Playlist e Fotos mostravam só o texto ou nada).
- **Aba Perfis**: criar, renomear, excluir (vai para a Lixeira) e escolher o perfil salvo. O nome do perfil é o nome do jogador. Dois interruptores por perfil, só em tempo de execução (o save não é alterado):
  - **Desbloquear cosméticos**: libera trajes, manobras, lápides, comemorações, veículos e crashtags do modo online.
  - **Desbloquear tudo (cosméticos e eventos)**: libera também os eventos da carreira.
- **Aba Controles**: troca das teclas que fazem o papel do controle de Xbox no teclado. São duas teclas por ação (acelerar, frear, direção, direcional, botões A/B/X/Y, LB/RB, View, Menu, L3/R3 e analógico direito), com opção de limpar e de voltar ao padrão. O mapeamento fica salvo no `onrush.ini` e vale no próximo JOGAR. Teclas opostas apertadas juntas (esquerda e direita, por exemplo) se anulam, em vez de valer a última.
- O direcional do teclado agora tem teclas próprias (1, 2, 3 e 4). Antes, W, S, A, D e as setas também apertavam o direcional, e acelerar com W abria a lista de provocações na corrida.
- **Tema claro e escuro**: escolha em Configurações → Tema do launcher. A troca é na hora e vale também para a janela de preparo dos shaders.
- **Discord Rich Presence**: o launcher tentava conectar só no instante em que o jogo abria. Agora tenta de novo a cada 15 s enquanto o jogo roda, então funciona com o Discord aberto depois do jogo ou reiniciado no meio. Se o Discord recusar a conexão ou a atividade, o motivo fica no log do launcher.
- **Minimizar o launcher ao jogar** (Configurações): o launcher minimiza quando o jogo abre e volta quando o jogo fecha.
- **Aba Fotos**: galeria das fotos tiradas no jogo (F12), com marcação de várias fotos, Marcar/Desmarcar todas, abrir foto, excluir marcadas (Lixeira) e abrir a pasta.
- A aba Jogar mostra o perfil em uso e as horas de jogo dele, e traz a escolha da pasta do jogo e do patch (a aba Instalação saiu).
- A opção de ir direto ao gameplay saiu.
- Novo visual: o logo **Rush Launcher** substitui o nome na lateral e vira o ícone da janela e do executável.
- A aba Playlist está escondida por enquanto, até a função ser melhor integrada ao jogo.
- **Preparação de shaders em uma etapa, sem abrir o jogo**: o launcher lê os shaders direto dos pacotes do jogo (`dat\`) em vez de abrir o jogo várias vezes para coletá-los. Tradução e compilação acontecem num único passe paralelo, com uma barra de progresso só. Todos os shaders passam a ser compilados com o DXC (o fxc levava até 1 minuto em alguns shaders de computação), então a etapa de "otimizar quando o jogo fecha" deixou de existir.
- Tradutor mais rápido, com o mesmo resultado byte a byte: o DXC roda dentro do processo (antes abria um `dxc.exe` por shader), até 16 processos em paralelo e leitura/padrões em cache no tradutor. Os ~13 mil shaders da versão final passam de 256 s para cerca de 3 minutos (16 threads).

### Jogo
- Porte para a versão 1.80.100.100.
- Corrigido o fechamento do jogo durante o vídeo de introdução quando "pular introdução" está desligado.
- As opções de inicialização do jogo agora chegam a ele (a ponte repassava os argumentos vazios).

### Gráficos
- Personagens e pilotos voltam a aparecer (cabine, menus, garagem).
- Rosto com a pele "cristal" e árvores como cartões cinza: causa comum no tradutor (uma leitura de constantes lia o endereço já sobrescrito). Tradutor revisão 4: ao subir de versão, o launcher retraduz os shaders sozinho.
- Árvores pretas corrigidas (profundidade dos passes de árvore).
- Telas "Escolha um veículo" e "Jogo pausado" voltam a mostrar o jogo desfocado ao fundo (antes ficavam pretas). Tradutor revisão 6.
- O reflexo de lente (lens flare) do sol não atravessa mais paredes e terreno. Tradutor revisão 5.
- **Sombras do sol**: o passe da máscara de sombra em cascata era traduzido com desvios por lane em vez de por onda de 64 lanes; o terreno distante saía todo em sombra e o chão do menu pré-corrida ficava branco. Agora o mapa inteiro recebe o sol e as sombras de árvores e do carro aparecem. O CS de iluminação principal da corrida usa o mesmo tratamento (cores do ambiente e da areia bem mais próximas do Xbox). Tradutor revisão 9.
- **Névoa e atmosfera**: o passe de névoa volumétrica (froxels) também usava desvios por lane. Com a correção a névoa/bruma volta: o menu pré-corrida ganha montanhas enevoadas, chão azul-claro e céu claro, e a corrida fica com cores de areia e mar bem mais naturais.
- **Partículas da GPU ligadas**: os 22 kernels emissores que ficavam desligados voltam a rodar (a guarda dos despachos indiretos evita o travamento antigo). Efeitos como brilho/aura elétrica azul, destroços e respingos passam a existir. Tradutor revisão 8: o launcher esvazia o `skip.txt` antigo sozinho.
- Shaders de captura do céu com desvios estruturados (tradutor revisão 7). O launcher retraduz os shaders sozinho ao subir de revisão.
- 12 dos 13 shaders que o tradutor recusava agora são traduzidos; os desenhos de shaders ainda não traduzidos são ignorados em vez de aparecerem como lixo.

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
- Logs enxutos: cada tipo de mensagem repetida aparece só algumas vezes, e cada log guarda só a sessão atual e a anterior (`*.prev.log`).
- **Modo de depuração** (Opções avançadas), desligado por padrão: log completo da bridge e capturas `frame_*.bmp` periódicas da tela.
- Alt+F4 e o botão de fechar da janela fecham o jogo.
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
