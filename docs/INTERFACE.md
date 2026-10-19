# Interface e controles — v1.2

O programa abre no launcher. Use Selecionar ROM para indicar ROM compatível (USA Rev 1); o payload é validado antes de habilitar Jogar. Uma ROM validada inicia diretamente e é lembrada para as próximas aberturas.

- Teclado: setas, Z = B, X = A, Shift = Select, Enter = Start, A/S = turbo, Esc = menu do PC. Todos são remapeáveis.
- Controle: seleção automática ou por dispositivo; botões físicos remapeáveis; D-pad e analógico; zona morta; teste de botão e hotplug. A configuração Menu é independente de Select. Guide/Home abre o menu por padrão; controles sem esse botão podem remapear a ação.
- USB/Bluetooth/receptores: SDL2 com HIDAPI e SDL_GameControllerDB. O Windows precisa reconhecer/parear o dispositivo. Não foi testado fisicamente cada modelo; o teste automatizado usa joystick virtual.
- Vídeo: proporção, escala inteira/ajuste/recorte, filtro, scanlines, brilho, resolução e tela cheia sem bordas. 16:9 amplia o cenário horizontal, com HUD original centralizado.
- Áudio: volume, mute, frequência 44,1/48/96 kHz, dispositivo e reserva de 20/40/60 ms. Saída NES mono nos dois canais.
- Aplicar e voltar salva as alterações; Descartar mantém as anteriores. Restaurar esta página afeta apenas o grupo atual e exige Aplicar para persistir.
- 1–5 selecionam o slot de save state. Carregar retoma a partida; Esc abre o menu; F11 alterna tela cheia.
- ROM, Projeto e Créditos descrevem o aplicativo, a ROM do usuário e a identidade Underplay.

Validação: smb3-ui-verify cria a interface sem abrir janelas visíveis, renderiza sete páginas, verifica Aplicar/Descartar e testa entrada, soltura e remoção de um controle virtual SDL. Capturas em build/ui-*.png. O teste do runtime continua sendo smb3-verify.

## Widescreen v1.3

Em Vídeo > Proporção, 16:9 agora amplia o cenário horizontal, mantendo o HUD centralizado e barras pretas apenas nas laterais dele. O ajuste da janela usa a proporção do canvas e preserva o tamanho relativo dos pixels, sem esticar a imagem. Não é o antigo 16:9 esticado. Esse modo é experimental: a ativação e o descarte horizontal dos objetos acompanham a área ampliada, e salas verticais ou sem espaço lateral disponível ficam no enquadramento nativo. Consulte WIDESCREEN.md.
