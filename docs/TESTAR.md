# Testar o release v1.5.0

1. Baixe o ZIP da página Releases e extraia tudo para uma pasta gravável.
2. Sem ROM cadastrada, abra MushroomRecompiled.exe. O launcher deve aparecer.
3. Em Selecionar ROM, indique ROM compatível (USA Rev 1, dump goodNES ou No-Intro), formato .nes. Uma ROM incompatível deve mostrar aviso sem iniciar o jogo.
4. A seleção válida inicia o jogo. Feche e reabra: deve iniciar diretamente usando a ROM lembrada. Se mover ou remover a ROM, deve voltar ao launcher. Confira abertura, início da partida, mapa e entrada em uma fase.
5. Teste Z/X/Enter/Shift e o teclado remapeado. Para gamepad, conecte/pareie no Windows e confira o dispositivo e o teste de botão na página Controle.
6. Confira áudio contínuo; se houver cortes, experimente a reserva Estável de 60 ms.
7. Esc abre o menu do PC; Start pausa o NES. Teste salvar/carregar e retomar.
8. Em Configurações, altere uma opção, descarte e veja se foi preservada a anterior. Depois aplique outra e reabra para conferir persistência.

## Relatar problemas

Abra uma Issue e informe versão, Windows, CPU/GPU, dispositivo de áudio, modelo/conexão do controle, sequência para reproduzir e uma captura quando útil. Informe região/revisão e hashes da ROM, sem anexar a ROM.

Não anexe ROM, saves ou arquivos de configuração pessoais. Não existe envio automático de telemetria ou diagnóstico.

## Limitações

Build experimental. O jogo completo, todos os modos de vídeo e todos os modelos físicos de controle não foram certificados. Há fallback interpretado para uma instrução na fronteira de banco no roteiro conhecido. Testes de tradução compartilham o modelo de hardware, portanto não constituem uma referência independente de console físico.

Para o widescreen v1.3, compare 4:3 e 16:9 na fase 1-1: confirme cenário adicional, pixels sem alongamento e o mesmo HUD entre barras pretas. Salas verticais e telas de mapa/título continuam em formato nativo. Consulte WIDESCREEN.md.

## Conferir versão e vídeo

Extraia a v1.5.0 em uma pasta nova e abra o executável dessa pasta. O título deve mostrar Recompiled e v1.5.0. Em Vídeo, selecione 16:9 · fase ampliada / HUD original e aplique. A indicação 16:9 · esticado pertence à versão antiga.

Nas fases horizontais suportadas, o cenário ocupa a largura maior e o HUD permanece centralizado com barras pretas nas laterais. Abra Configurações com Esc; alterne páginas, redimensione e retorne à página Vídeo. Logo e arte do launcher não devem aparecer sobre os campos.

## Inimigos na área ampla — v1.4

Na fase 1-1 em 16:9, confira o inimigo do primeiro cano na área à direita antes de o cano entrar nos 256 pixels originais. Avance e volte: inimigos devem continuar na faixa lateral, sem desaparecer na antiga borda. Confira colisões, derrota por pulo, cascos, pausa, salvar/carregar e retorno ao mapa. Refaça em 4:3 para comparar os gatilhos originais. Em fases cheias, a capacidade original de cinco objetos principais ainda pode limitar novas ativações. Relate fase, tipo de inimigo e posição da câmera em caso de falha.

## Cenas e plataformas — v1.5

Confira a tela de prêmio ao concluir a fase: nenhuma coluna do mapa anterior deve aparecer nas margens. Bônus e cartas usam o quadro nativo centralizado. Nas fases altas, atravesse as antigas bordas observando blocos, plataformas e seus contornos. O novo teste automatizado compara o fundo real da PPU e o fundo ampliado em duas fases, além de simular os indicadores de prêmio, bônus e saída.
