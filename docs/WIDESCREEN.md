# Widescreen experimental — v1.3

A opção Vídeo > Proporção > 16:9 amplia a área da fase sem redimensionar horizontalmente os sprites e tiles. O canvas tem 426 × 240 pixels (16:9 arredondado para largura par). Os pixels são escalados uniformemente na janela. O modo novo é o padrão de configurações novas; configurações antigas podem selecionar a opção manualmente.

## HUD

As linhas 192–239 são copiadas do framebuffer original de 256 × 240, sem redesenhar ou reorganizar vidas, pontuação, tempo e itens. O HUD fica centralizado entre duas barras pretas de 85 pixels. A expansão só é aplicada acima dessa faixa.

## Cenário real

O compositor lê o mapa de blocos completo da fase em SRAM e as tabelas de metatiles da ROM fornecida, em vez de repetir bordas, ampliar um recorte ou desenhar uma decoração genérica. CHR, paleta e câmera são observados na scanline 8, antes da troca de gráficos para o HUD. A câmera ampla é limitada às bordas da fase. O conteúdo nativo do jogo é preservado; a faixa de oito pixels que o NES oculta na borda esquerda recebe fundo real quando passa a ficar dentro do campo ampliado.

O componente é de apresentação: não escreve no CPU, no mapa, no mapper ou na física. Sprites já avaliados que cruzam a borda direita podem ser completados fora do recorte original.

## Limites

- A ativação, IA e descarte de inimigos continuam nas regras originais de 256 pixels. Um inimigo fora dessa área pode aparecer apenas quando o jogo o ativar; não é uma simulação ampliada de objetos.
- Mapas, títulos, bônus, Toad Houses, salas verticais e locais sem largura de fase suficiente mantêm a imagem original centralizada. Não se inventa conteúdo fora do mapa.
- Efeitos especiais de raster, água e todas as fases ainda não foram validados. O modo é experimental.
- No modo amplo, o canvas inteiro é ajustado à janela; não se recorta o HUD para preencher uma proporção diferente.

## Teste

```powershell
cd build
.\smb3-wide-verify.exe "..\Super Mario Bros. 3 (USA) (Rev 1).nes"
```

Roteiro entra na fase 1-1 e desloca a câmera. Em 1.800 quadros, 630 foram compostos como fase ampliada e a câmera chegou a 456 pixels. O teste confere byte a byte o HUD e o miolo nativo, as barras pretas e a igualdade de todos os hashes de máquina entre widescreen ligado e desligado. Isso verifica que o compositor não altera a simulação; não comprova todas as fases.

Capturas de referência: docs/images/widescreen.png e widescreen-start.png. Créditos técnicos: Southbird, Super Mario Bros. 3 Full Disassembly, https://github.com/captainsouthbird/smb3 e http://www.sonicepoch.com/sm3mix/ . O disassembly foi consultado para pesquisa; o compositor lê os dados da cópia local do usuário.
