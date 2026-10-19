# Estado — v1.1

## Correção da imagem e execução

Substituído o adaptador de execução legado, baseado em NMI/fibers, pelo backend cycle-accurate já incluído no NESRecomp local. CPU, PPU, APU, DMA, mapper MMC3 e interrupções passam a compartilhar o relógio da máquina. A interface Windows continua usando runtime/glue.h.

Os blocos C foram regenerados pelo compilador construído dos mesmos fontes do runtime. Entradas observadas no teste foram preservadas em docs/smb3-cycle-seeds.txt. O executável anterior apresentava 541 BRKs inesperados em 600 quadros; o novo backend não depende das continuações C e da tabela de despacho legadas.

## Áudio

- A fila WinMM espera a conclusão de um buffer antes de reutilizá-lo e trata erro de submissão.
- A geração usa a frequência configurada: 44100, 48000 ou 96000 Hz.
- A quantidade drenada é a quantidade efetivamente produzida pelo relógio, sem forçar 800 amostras por quadro.
- O limitador de quadros acumula prazos e usa resolução de timer de 1 ms, evitando acumular o arredondamento de Sleep a cada quadro.
- A saída tem dois canais com o mesmo sinal mono do NES, sem ganho artificial diferente por canal.

## Validação executada

Build Release: aplicativo v1.1 e smb3-verify compilados.

Teste headless de 3.000 quadros: 1.316 mudanças de imagem, 2.395.990 amostras mono produzidas; nenhum CPU jam ou execução interpretada em RAM/I/O. 89.334.341 ciclos nativos e 4.770 interpretados: aproximadamente 99,995% nativo. A única entrada não compilada encontrada foi banco 4K $14, CPU $CFFF, cujo opcode cruza a fronteira de banco; o fallback executa essa instrução.

Todos os 3.000 hashes de estado coincidiram entre execução recompilada e interpretada. Isso valida a tradução sob o mesmo modelo de hardware; não é uma comparação independente com console físico.

Save/load completo: execução por mais 30 quadros após restauração produz o mesmo hash de memória/máquina. O backend novo serializa estado completo e não restaura as antigas fibers. Saves antigos são incompatíveis e rejeitados.

Áudio por 60 quadros: 43.968 amostras a 44,1 kHz; 47.857 a 48 kHz; 95.714 a 96 kHz. O teste admite a diferença inicial de fase do primeiro quadro. Reprodução audível no dispositivo do usuário ainda requer reteste.

Logs: build/verify-cycle.log e build/verify-cycle.err.log. Capturas: build/verify-frame-180.png, -360.png, -600.png, -900.png, -1200.png e -3000.png.

## Escopo restante

Validar manualmente controles e áudio no Windows do usuário e ampliar a cobertura de gameplay/fases. O teste atual não comprova o jogo inteiro. ROM e pasta de origem foram preservadas.

## Interface v1.2

Launcher abre antes da partida, com ROM validada, controles, configurações, jogo e save states. Telas internas reescritas com sete abas, aplicar/descartar/restaurar, seletores próprios e navegação por teclado/gamepad. Artes clássicas verificadas e fontes em ARTES.md.

Volume, brilho, scanlines, resolução, tela cheia, dispositivo de áudio e frequência agora são aplicados. Reserva de áudio usa blocos de 10 ms para 20/40/60 ms de prefill. Select não abre mais o menu do PC. Corrigido fechamento duplo do joystick ao desconectar um GameController SDL. Preferência de dispositivo usa caminho estável, com GUID como fallback.

smb3-ui-verify passou: renderização das sete páginas, aplicar/descartar, entrada e soltura de botão virtual, hotplug. O teste de 3.000 quadros do runtime continua passando. Não houve validação física de todos os controles USB/Bluetooth.

## v1.3 — prévia widescreen

Canvas 426 × 240 com expansão real de cenário em fases horizontais e HUD original centralizado, entre barras pretas. Compositor lê mapa em SRAM e gráficos observados antes do IRQ do HUD. Não modifica CPU, física ou lógica de inimigos.

Teste widescreen: 1.800 hashes de máquina idênticos entre modo ligado/desligado; 630 quadros de fase ampliada; câmera até 456 pixels; HUD e barras verificados. Limites e referências em WIDESCREEN.md. A ativação de objetos permanece original; salas verticais e cenas não suportadas ficam em formato nativo.

## v1.4 — inimigos na área ampliada

No modo 16:9, registros normais de objetos são ativados com base na câmera ampla, e o descarte horizontal usa suas bordas. Eventos e o cartão final mantêm seus gatilhos; continuam cinco slots principais. OAM adicional é gerada em cópias isoladas da máquina para desenhar os objetos nas margens ampliadas. Essa geração não altera a partida real; a ativação antecipada altera a sequência de gameplay em relação ao modo nativo.

O teste atual compara 1.800 estados nativos/interpretados com a mesma política ampla, verifica HUD/barras/miolo, reparo das bordas, inimigos adiantados e retidos, índices de spawn únicos e save/load. Um controle sem widescreen comprova a diferença de ativação da primeira Piranha. Cobertura de todas as fases ainda pendente.

Beta testers: ALEISTECH e RETROCESSO, creditados no README e na página Créditos do aplicativo.
