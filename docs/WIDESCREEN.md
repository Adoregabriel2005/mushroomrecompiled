# Widescreen experimental — v1.5.0

Vídeo > Proporção > 16:9 amplia a fase em um canvas 426 × 240, sem esticar sprites ou tiles. A câmera ampla respeita as bordas do mapa. As linhas 192–239 mantêm o HUD original de 256 pixels, centralizado entre duas faixas pretas de 85 pixels.

## Cenário e bordas

O compositor lê o mapa completo em SRAM e as tabelas de metatiles da ROM fornecida. CHR, paleta e câmera são observados na scanline 8, antes da troca de gráficos para o HUD. O miolo nativo de 224 pixels é preservado. As faixas de 16 pixels nas extremidades da imagem original são reconstruídas pelo mapa para remover tiles e atributos parcialmente atualizados na VRAM. Sprites dessas faixas são restaurados da OAM capturada.

## Ativação e permanência dos objetos

A partir da v1.4, a simulação do modo amplo também muda: os registros reais da fase são ativados na área ampliada, com margem de 16 pixels à direita e 64 à esquerda para cobrir objetos grandes. Usa os cinco slots principais originais, sem substituir objetos ativos, duplicar índices de spawn ou antecipar controladores de eventos e o cartão de fim de fase. Os registros só são usados após a inicialização da cena.

Os objetos novos entram no estado de inicialização do próprio jogo. Movimento, animação, IA e colisões continuam executados pelo código recompilado. As comparações de descarte horizontal passam a usar as bordas da câmera ampla, conservando as margens específicas de cada rotina. A regra vertical de descarte permanece original.

O renderizador gera OAM adicional usando a rotina de estado do objeto em uma cópia isolada da máquina, com a câmera deslocada e a pausa de gameplay ativada. Apenas os sprites resultantes são retidos para desenhar fora da janela nativa. CPU, RAM, mapper e relógio dessa cópia são descartados ao retornar; movimento especulativo não é aplicado à partida. O desenho principal e o HUD continuam originais.

A ativação antecipada e o descarte mais distante mudam a sequência da partida em relação ao modo nativo. A verificação de determinismo compara execução recompilada e interpretada com a mesma política ampla; já não se espera igualdade entre widescreen ligado e desligado.

## Limites

- A capacidade continua em cinco slots principais: fases cheias podem aguardar um slot livre. Objetos especiais, projéteis e controladores mantêm seus gatilhos originais.
- Mapas, títulos, bônus, Toad Houses, salas verticais, transições e locais sem largura suficiente mantêm o enquadramento original.
- Todas as fases, chefes, efeitos de água/raster e combinações de inimigos ainda precisam de validação. O modo é experimental.
- O canvas inteiro é ajustado à janela, mantendo o HUD e a proporção dos pixels.
- Desativar 16:9 restaura a política original de spawn e descarte. Objetos já ativados seguem seu estado corrente até o jogo descartá-los.

## Testes

```powershell
cd build
.\smb3-wide-verify.exe "..\game.nes"
```

O roteiro entra em 1-1 e rola a câmera. Confere o HUD, miolo nativo e barras pixel a pixel; injeta lixo nas bordas para conferir o reparo; verifica inimigos ativos fora da área antiga e retidos além do antigo limite de descarte; recusa índices de spawn duplicados. Uma Piranha no primeiro cano deve estar ativa e ter sprites na margem ampla, enquanto a mesma sequência em modo nativo ainda não a ativa.

Também compara 1.800 estados recompilados/interpretados com inimigos ampliados, verifica continuação determinística após save/load e confirma que a geração isolada de sprites não altera a máquina real. Esses testes cobrem o roteiro, sem comprovar todas as fases.

Pesquisa técnica: [Southbird — Disassembly de referência](https://github.com/captainsouthbird/smb3), especialmente PRG000 e PRG005; [SM3Mix](http://www.sonicepoch.com/sm3mix/). O disassembly foi consultado para pesquisa; dados e gráficos vêm da cópia local do usuário. Sobre artefatos originais: [NESdev Mirroring](https://www.nesdev.org/wiki/Mirroring) e [Attribute tables](https://www.nesdev.org/wiki/PPU_attribute_tables).

## v1.5 — transições e bordas de objetos

A expansão é desativada durante prêmio, cartões, raster especial e saída da fase. O centro usa exatamente o quadro nativo e todas as margens externas são pretas, sem reaproveitar o mapa anterior. O renderer cobre os oito slots de objetos existentes sem aumentar a capacidade de ativação dos cinco principais. As peças visíveis da OAM permitem alinhar a camada isolada ao quadro real, e sua cobertura também inclui as faixas de 16 pixels reconstruídas nas antigas bordas.

O teste scenes_verify usa troca de destino e posicionamento apenas no processo de teste para percorrer duas fases altas, comparar o fundo reconstruído ao fundo real da PPU e simular os indicadores de prêmio, bônus, raster e saída. Essas intervenções não integram o aplicativo.

O enquadramento usa o scroll realmente aplicado pela PPU, reconstruído no espaço do mapa. A câmera de gameplay em RAM pode estar alguns pixels à frente do quadro exibido; usá-la diretamente criava emendas entre as margens reconstruídas e o centro nativo, especialmente nas fases altas com deslocamento horizontal.
