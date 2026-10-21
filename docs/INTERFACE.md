# Interface e controles

O programa é um binário único: dois cliques iniciam o jogo direto. Sem `--rom` e sem ROM lembrada, um único `.nes` próximo (ao lado do app, no diretório atual ou em `roms/`) abre direto; com zero ou vários, abre um seletor dentro da janela (setas/Enter, gamepad, ou arrastar o arquivo para a janela). A ROM usada é validada e lembrada por caminho absoluto em `config.ini` para as próximas aberturas.

- Teclado: setas, X = A, Z = B, Shift = Select, Enter = Start. Tudo remapeável via `key_*` em `config.ini`.
- Menu do PC (Esc ou botão Guide): RESUME, SAVE STATE, LOAD STATE, WIDESCREEN, FULLSCREEN, QUIT. Navega com setas/WASD ou D-pad; Enter/Espaço/A/Start ativa, B fecha.
- Atalhos: F5 salva, F7 carrega, 1–5 trocam o slot de save state. F10 alterna widescreen, F11 tela cheia, F8 mudo, -/= volume. G grava a sessão em `saves/replay-N.rec`. R é o Reset do console (mantém a SRAM).
- Controle: abertura automática com hotplug; D-pad e analógico esquerdo (zona morta ~12000); A/B/X/Y, Back = Select, Start = Start. Guide abre o menu.
- Vídeo: `--renderer vulkan|sdl` (Vulkan com fallback para SDL2), `--scale 1-8`, aspecto fit/integer/stretch, `--scanlines`, `--brightness 50-150`, tela cheia. 16:9 amplia o cenário horizontal com HUD original centralizado; salas verticais e cenas voltam ao quadro nativo.
- Áudio: volume, mudo, `--audio-rate 44100|48000|96000`. Saída NES mono nos dois canais.
- Persistência: `config.ini` guarda ROM, vídeo, áudio e teclas. Progresso em `saves/smb3.srm` (janela $6000–$7FFF), save states em `saves/state-1..5.sst`.
- Linha de comando: `--record/--replay` (1 byte de controle por quadro), `--frames N` e `--screenshot` para testes, `--menu` inicia pausado, `--slot 1-5`, `--no-vsync`, `--help` lista tudo.

Ver trocas de proporção e ativação de inimigos em 1-1 em WIDESCREEN.md. Limitações e garantias do build em PORTABLE.md.
