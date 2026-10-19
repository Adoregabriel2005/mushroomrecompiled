# Testar o release v1.2.0

1. Baixe o ZIP da página Releases e extraia tudo para uma pasta gravável.
2. Abra SuperMarioBros3Recomp.exe. O launcher deve aparecer antes da partida.
3. Em Selecionar ROM, indique Super Mario Bros. 3 (USA) (Rev 1), formato .nes. Uma ROM incompatível deve mostrar aviso sem iniciar o jogo.
4. Escolha Jogar. Confira abertura, início da partida, mapa e entrada em uma fase.
5. Teste Z/X/Enter/Shift e o teclado remapeado. Para gamepad, conecte/pareie no Windows e confira o dispositivo e o teste de botão na página Controle.
6. Confira áudio contínuo; se houver cortes, experimente a reserva Estável de 60 ms.
7. Esc abre o menu do PC; Start pausa o NES. Teste salvar/carregar e retomar.
8. Em Configurações, altere uma opção, descarte e veja se foi preservada a anterior. Depois aplique outra e reabra para conferir persistência.

## Relatar problemas

Abra uma Issue e informe versão, Windows, CPU/GPU, dispositivo de áudio, modelo/conexão do controle, sequência para reproduzir e uma captura quando útil. Informe região/revisão e hashes da ROM, sem anexar a ROM.

Não anexe ROM, saves ou arquivos de configuração pessoais. Não existe envio automático de telemetria ou diagnóstico.

## Limitações

Build experimental. O jogo completo, todos os modos de vídeo e todos os modelos físicos de controle não foram certificados. Há fallback interpretado para uma instrução na fronteira de banco no roteiro conhecido. Testes de tradução compartilham o modelo de hardware, portanto não constituem uma referência independente de console físico.
