# Widescreen experimental — v1.5.0

Vídeo > Proporção > 16:9 amplia a fase em um canvas 426 × 240, sem esticar sprites ou tiles. A câmera ampla respeita as bordas do mapa. O quadro central e o HUD (linhas 192–239) têm o overscan cortado — 12 pixels de cada lado, como no smb3pc — e ficam centralizados com 232 pixels, entre duas faixas pretas de 97 pixels.

O HUD é uma camada própria, como no `src/runtime/hud.cpp` do smb3pc, e o bloco é copiado dela com o overscan cortado e centralizado. A linha em que a barra começa vem do IRQ de scanline do cartucho: o mapper registra o último IRQ do quadro e a barra começa na linha seguinte (240 quando o quadro não tem split). A partir dali, a primeira linha que de fato pertence à barra é a primeira que não continua a linha anterior na captura por scanline do quadro — o `hud_bar_first()` da referência —, e a linha de onde o conteúdo é desenhado é a primeira abaixo dela que segue até o fim da imagem com os bancos de CHR da barra. As linhas que o split do meio do quadro encena — a primeira ou duas linhas da área do status, que chegam ao vídeo como faixa sólida da cor de fundo do cenário (azul ao ar livre) ou do cartão — são desenhadas a partir da âncora: o fundo vem do nametable do próprio quadro e do CHR no scroll da barra, e os sprites daquela linha vêm da OAM capturada, como o hardware faria. A barra então começa na sua primeira linha e nada encenado aparece acima dela. A linha de ancoragem também é redesenhada, porque a PPU buscou seus primeiros tiles antes da escrita do split: é ali que sobra um resto da linha de cima nas primeiras colunas. As linhas seguintes do HUD saem idênticas ao quadro nativo — o desenho as reproduz pixel a pixel —, e as linhas acima da barra mantêm os pixels da fase, então o chão vai até a última scanline do playfield. Um quadro sem split (título, mapa, menu) não ganha camada nenhuma. O ajuste vale também em 4:3: o quadro apresentado sai da mesma camada, mas o quadro interno da máquina, os hashes e o save state não mudam.

## Cenário e bordas

O compositor lê o mapa completo em SRAM e as tabelas de metatiles da ROM fornecida. CHR, paleta e câmera são observados na scanline 8, antes da troca de gráficos para o HUD. O miolo nativo de 232 pixels é preservado. As faixas de 12 pixels nas extremidades da imagem original são reconstruídas pelo mapa para remover tiles e atributos parcialmente atualizados na VRAM (quatro colunas além do blanking de 8 pixels do próprio NES). Sprites dessas faixas são restaurados da OAM capturada.

## Ativação e permanência dos objetos

A partir da v1.4, a simulação do modo amplo também muda: os registros reais da fase são ativados na área ampliada. Usa os cinco slots principais originais, sem substituir objetos ativos, duplicar índices de spawn ou antecipar controladores de eventos e o cartão de fim de fase. Os registros só são usados após a inicialização da cena. Logo após o carregamento, uma saída curta ativa também os registros já visíveis nas margens (coberta pela revelação do modo); em regime, só registros estritamente fora da imagem são ativados, para que nada apareça de repente no meio do jogo. Dois slots livres são sempre reservados ao gerador original.

Os objetos novos entram no estado de inicialização do próprio jogo. Movimento, animação, IA e colisões continuam executados pelo código recompilado. As comparações de descarte horizontal passam a usar as bordas da câmera ampla, conservando as margens específicas de cada rotina. A regra vertical de descarte permanece original.

O renderizador reexecuta a rotina de estado do objeto em uma cópia isolada da máquina, com o objeto no meio da tela para que nenhuma peça seja cortada na borda (peças cortadas e depois misturadas ao quadro eram a causa do tremor). Antes de chamar, rebobina o estado dinâmico do slot às posições pré-atualização guardadas do quadro anterior: a rotina move o objeto ao desenhar, e sem o rebobino as peças sairiam uma fase de movimento à frente do quadro (o balanço da planta, o passo do inimigo) — nenhum voto de posicionamento conserta peças com fase errada. Todo esse estado é guardado em vetores por slot (`$91`, `$76`, `$A3`, `$88`, `$BE`, `$D0`, `$689`, `$691`, `$9A`, `$669`, `$679`, `$AC`, `$B5`, `$651`, `$681`, `$D9`, `$518`, `$520`, `$7F`, `$7FD0`, `$C6`): o índice tem de ser somado em cada leitura e escrita. Sem ele o rebobino caía sempre no slot 0 e o objeto sob teste continuava com os valores já avançados — um tique de animação e um quadro de movimento à frente da imagem, que é o tremor do caule. Retém apenas peças relativas à origem rebobinada (deslocamento, padrão, atributos e ordem na OAM).

A base de sprites da reexecução é fixa por slot (`$058F = $40 + slot*16`), dentro da página da OAM. O gerador do jogo gira essa base a cada quadro e os desenhadores a avançam com soma de 8 bits: quando ela caía em `$E8` o bloco da planta atravessava `$02FF`, o caule ia parar no offset 0 e a correção de paleta (`Sprite_RAM+$0A,Y`) era escrita em `$0302`, fora da página que a PPU transfere — o caule ficava com a paleta da cabeça por um quadro e piscava. O offset absoluto não importa para as peças (são relativas), e a base por slot preserva a ordem de prioridade que a passagem das margens usa para ordenar as peças. Dois pontos de OAM com o mesmo padrão e bits de espelhamento votam no posicionamento mesmo que a paleta discorde (o jogo pode discordar de si mesmo por um quadro); já a reivindicação que desliga a reparação exige atributos exatos, porque reivindicar demais deixa a faixa nativa sem pintura e o lixo dela aparece na tela. O instantâneo só gira quando a máquina avançou: duas apresentações do mesmo quadro (o teste das bordas faz isso) têm de gerar as mesmas peças, e regravar o alvo do rebobino com o estado pós-atualização desenhava outro tique de animação.

Cada objeto ancora sua origem ao par (peça, sprite) com mais votos por deslocamento — a região de sprites gira e deixa cópias velhas dos mesmos padrões exatamente 8 pixels de distância, e ancorar numa delas deslocava o sprite inteiro. Só slots a até 2 pixels da âncora são reivindicados; o resto pertence à reparação do quadro, que os mostra exatamente como a PPU os desenhou. Sem par (todo na margem), mantém a âncora anterior. Cada slot da OAM tem exatamente um dono: a reparação das faixas pula os slots reivindicados e a passagem das margens os desenha inclusive nas faixas, de modo que um sprite sobre a borda nunca é pintado duas vezes lado a lado — era isso que engrossava as linhas. CPU, RAM, mapper e relógio dessa cópia são descartados ao retornar; movimento especulativo não é aplicado à partida. O desenho principal continua original, e do HUD só as linhas encenadas pelo split são reconstruídas pela camada da barra. Plantas que pulam um quadro de desenho seguram as peças anteriores por um quadro, como no jogo.

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

O roteiro entra em 1-1 e rola a câmera. Confere o HUD, miolo nativo e barras pixel a pixel — inclusive que cada linha encenada pelo split continue o fundo da própria barra, e não o do cenário, que toda linha abaixo da âncora reproduza exatamente o quadro da PPU e que um quadro sem IRQ de split não ganhe camada —; injeta lixo nas bordas para conferir o reparo; verifica inimigos ativos fora da área antiga e retidos além do antigo limite de descarte; recusa índices de spawn duplicados. Uma Piranha no primeiro cano deve estar ativa e ter sprites na margem ampla, enquanto a mesma sequência em modo nativo ainda não a ativa.

Também compara 1.800 estados recompilados/interpretados com inimigos ampliados, verifica continuação determinística após save/load e confirma que a geração isolada de sprites não altera a máquina real. Esses testes cobrem o roteiro, sem comprovar todas as fases.

Pesquisa técnica: [Southbird — Disassembly de referência](https://github.com/captainsouthbird/smb3), especialmente PRG000 e PRG005; [SM3Mix](http://www.sonicepoch.com/sm3mix/). O disassembly foi consultado para pesquisa; dados e gráficos vêm da cópia local do usuário. Sobre artefatos originais: [NESdev Mirroring](https://www.nesdev.org/wiki/Mirroring) e [Attribute tables](https://www.nesdev.org/wiki/PPU_attribute_tables).

## v1.8 — split do IRQ e nametables do quadro

A linha do split não é derivada da captura: o mapper registra o último IRQ de scanline do quadro (`cyc_frame_split_line`) e a barra começa na linha seguinte, exatamente como o `hud_top` do smb3pc. Um quadro sem esse IRQ não ganha camada — título, mapa, menus e efeitos de raster ficam como a máquina os desenhou, sem uma varredura que tente adivinhar onde a barra começa. A partir do split, a primeira linha da barra e a âncora continuam saindo da captura por scanline, como no `hud_bar_first()`/`hud_settled_row()` de referência.

O fundo de uma linha não é mais lido da CIRAM viva. No fim do quadro (linha 241, ponto 1), antes de o handler de NMI do jogo reescrever tiles para o próximo quadro, o runner fotografa os quatro quadrantes do nametable como o PPU os mapeia; a camada desenha a partir dessa fotografia. Sem ela, o streaming de colunas do nível já tinha mudado os tiles de baixo quando a camada era composta, e a reconstrução saía diferente do quadro.

A cor de cada linha aplica também o greyscale do $2001 (bit 0), como o `color_mask` da referência e o `output_pixel` da PPU; antes a camada desenhava em cores quando o jogo tinha o vídeo em tons de cinza. Os sprites da barra usam a linha `(y - 1) - OAM Y`, a mesma da referência: os padrões de uma scanline foram buscados durante a anterior.

A única parte do `hud.cpp` que não foi portada é o cache entre quadros (`hud_matches`/`hud_snapshot`): a camada é reconstruída a cada apresentação, com os mesmos pixels que o cache devolveria.

Conferido no roteiro de 1.800 quadros: 1.749 com barra, 1.139 linhas encenadas, e **zero** diferenças entre a camada e a máquina fora do bloco encenado — inclusive em 1.507 linhas de barra com sprites. As linhas acima da barra não são redesenhadas: o jogo troca do cartão para a fase no meio de uma delas (a escrita do scroll cai dentro da linha) e a captura é um estado por linha inteira; redesenhá-las como o `hud_build()` faz custa 23 pixels em 2 dos 1.749 quadros, então a camada preserva os pixels da máquina ali, que são exatos por construção.

## v1.7 — camada do HUD

O HUD passa a ser uma camada própria, portada de `src/runtime/hud.cpp` do smb3pc, no lugar da regra anterior que procurava uma linha de cor única acima da barra. Aquela regra acertava a fase ao ar livre (barra preta) e errava o cartão de início de fase, onde a barra é o painel do cartão: o fundo do HUD não é sempre preto, e a linha encenada tem de ser desenhada da própria barra. A camada também só toca linhas cuja captura tem rendering ligado, como o `hud_blit` de referência; uma linha que o jogo deixou em branco continua sendo a do quadro da máquina.

A quebra que separa fase e barra não vem de uma constante: sai da captura por scanline do próprio quadro. O primeiro ponto a partir do split que não continua a linha anterior (scroll vertical, fine X ou seleção de tabela de padrões) é a primeira linha do HUD; a primeira linha abaixo dele que segue até o fim da imagem é a âncora do conteúdo. O desenho usa o scroll da âncora, e reproduz o fundo e os sprites da barra exatamente: redesenhadas a partir da captura, as linhas já assentadas do HUD batem 100% com os pixels que o jogo desenhou. As linhas encenadas usam esse mesmo desenho, só que no scroll da âncora.

## v1.6 — janela de objetos e gancho de carregamento

O gancho que avisa o carregamento de fase (`smb3_wide_level_load`) era emitido fora da condição que o protegia: o gerador de código o colocava antes de *toda* instrução, de modo que a janela de saída curta (`load_grace`) era rearmada a cada quadro e ficava permanentemente aberta. Com isso os registros das margens eram criados repetidamente, inclusive dentro da imagem visível — um registro em x=224 antigo surgia em cima do Mario. O gancho agora fica dentro do bloco do endereço de reset de som (uma única chamada, em `$A242`), e a janela também é armada ao entrar numa cena de jogo, sinal visível em RAM nos dois motores: o modo interpretado (F2) não executa o gancho gerado e divergia da execução recompilada. A criação de registros exige que eles estejam fora da imagem nativa mesmo na saída curta, então nada aparece de repente.

## v1.5 — transições e bordas de objetos

A expansão é desativada durante prêmio, cartões, raster especial e saída da fase. O centro usa exatamente o quadro nativo e todas as margens externas são pretas, sem reaproveitar o mapa anterior. O renderer cobre os oito slots de objetos existentes sem aumentar a capacidade de ativação dos cinco principais. As peças relativas dispensam qualquer alinhamento com a OAM do quadro, e sua cobertura também inclui as faixas de 12 pixels reconstruídas nas antigas bordas.

O teste scenes_verify usa troca de destino e posicionamento apenas no processo de teste para percorrer duas fases altas, comparar o fundo reconstruído ao fundo real da PPU e simular os indicadores de prêmio, bônus, raster e saída. Essas intervenções não integram o aplicativo.

O enquadramento usa o scroll realmente aplicado pela PPU, reconstruído no espaço do mapa. A câmera de gameplay em RAM pode estar alguns pixels à frente do quadro exibido; usá-la diretamente criava emendas entre as margens reconstruídas e o centro nativo, especialmente nas fases altas com deslocamento horizontal.
