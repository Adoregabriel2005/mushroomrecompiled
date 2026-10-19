#pragma once
/* Resumos editoriais originais e pesquisa histrica para Super Mario Bros. 3 Recomp.
   Fontes e metodologia detalhadas em docs/HISTORIA.md.
   Nenhum artigo proprietrio ou vdeo de terceiros foi empacotado aqui. */

typedef struct {
    const wchar_t *name;
    const wchar_t *kicker;
    const wchar_t *body;
    const wchar_t *source_label;
    const wchar_t *source_url;
    const wchar_t *other_label;
    const wchar_t *other_url;
} HistoryChapter;

static const HistoryChapter history_chapters[] = {
{
    L"A Grande Aventura",
    L"1988 Famicom  1990 NES  Nintendo  1-2 jogadores  8 mundos",
    L"Lanado para o Famicom no Japo em 1988 e para o NES no Ocidente em 1990, Super Mario Bros. 3 "
    L"representa o pice do design de plataforma em 8 bits sob liderana de Shigeru Miyamoto e Takashi Tezuka. [1]\n\n"
    L"Bowser e seus sete Koopalings (Larry, Morton, Wendy, Iggy, Roy, Lemmy e Ludwig) invadiram os reinos "
    L"do Mushroom World, roubando as varinhas dos reis e transformando-os em animais. Mario e Luigi partem "
    L"em uma jornada monumental atravs de 8 mundos temticos: Grass, Desert, Sea, Giant, Sky, Ice, Pipe e Dark Land.\n\n"
    L"O jogo introduziu mapas do mundo com caminhos ramificados, casas de Toad, jogos de roleta e os temveis "
    L"Navios Voadores (Airships). Dentre os power-ups revolucionrios esto a Super Leaf (Mario Guaxinim capaz "
    L"de voar com cauda), Frog Suit, Tanooki Suit, Hammer Suit, P-Wing e a Bota de Goomba (Kuribo's Shoe). [1, 2]",
    L"[1] Ficha Nintendo / Iwata Asks",
    L"https://iwataasks.nintendo.com/interviews/wii/nsmb/0/2/",
    L"[2] Acervo Super Mario Bros. 3",
    L"https://www.mariowiki.com/Super_Mario_Bros._3"
},
{
    L"Revoluo Tcnica e o Chip MMC3",
    L"Como a Nintendo R&D4 superou os limites do hardware NES",
    L"O hardware do NES foi concebido originalmente com limitaes severas de memria e rolagem (scrolling). "
    L"Para Super Mario Bros. 3, a equipe de engenharia da Nintendo desenvolveu o ASIC proprietrio MMC3 "
    L"(Memory Management Controller 3, Mapper 4), um divisor de guas para o console. [3]\n\n"
    L"O chip viabilizou o chaveamento dinmico de bancos de memria (PRG de 256 KB e CHR de 128 KB, somando 384 KB). "
    L"Seu recurso mais sofisticado  o contador de scanlines acionado por pulsos de endereo da PPU (linha A12). [3]\n\n"
    L"Quando o contador atinge zero, o MMC3 gera uma interrupo IRQ com preciso de scanline. Isso permitiu "
    L"que a interface inferior (barra de status com vidas, pontos, cartas e o medidor P-Meter) ficasse perfeitamente "
    L"imvel na tela, enquanto o restante do cenrio rola livremente na horizontal, vertical e diagonal. [3, 4]",
    L"[3] Nesdev MMC3 Technical Reference",
    L"https://www.nesdev.org/wiki/MMC3",
    L"[4] TCRF Technical Research",
    L"https://tcrf.net/Super_Mario_Bros._3"
},
{
    L"A Trilha Sonora de Koji Kondo",
    L"Percusso digital DPCM, ritmos sincopados e dinamismo",
    L"O lendrio compositor Koji Kondo utilizou o sintetizador de som Ricoh 2A03 com um nvel de virtuosismo "
    L"indito. Super Mario Bros. 3 foi o primeiro jogo da srie a explorar intensamente o canal DPCM do console, "
    L"tocando amostras de percusso digitalizada como congas, caixas e timbales. [1, 5]\n\n"
    L"Os temas combinam influncias de reggae, jazz swingado e marchas orquestrais. O tema de Grass Land "
    L"possui linha de baixo ativa e compasso alegre; a msica do Airship evoca tenso teatral; e as casas de fada "
    L"transmitem magia e serenidade com arpejos cintilantes nos canais de onda quadrada.\n\n"
    L"A trilha  viva: correr a toda velocidade enche o medidor P-Meter, acompanhado de efeitos sonoros ritmados "
    L"que aceleram a sensao de decolagem do Mario Raccoon. A obra de Kondo permanece atemporal. [5]",
    L"[5] Nintendo Music Analysis",
    L"https://iwataasks.nintendo.com/interviews/wii/mario25th/sound/0/0/",
    L"[1] Entrevista Koji Kondo",
    L"https://iwataasks.nintendo.com/interviews/wii/nsmb/0/2/"
},
{
    L"O Fenmeno e Publicidade",
    L"Do clmax em O Gnio do Videogame (The Wizard)  loucura nas lojas",
    L"Antes de chegar s lojas americanas em fevereiro de 1990, Super Mario Bros. 3 foi protagonista "
    L"de uma das campanhas de marketing mais bem-sucedidas do entretenimento. O jogo fez sua estreia mundial "
    L"como a grande revelao do campeonato no filme 'The Wizard' (O Gnio do Videogame, Universal Pictures, 1989). [6]\n\n"
    L"A cena em que a 'Warp Whistle'  encontrada na fase 1-3 virou segredo comentado em ptios escolares por todo o mundo. "
    L"Os comerciais de televiso mostravam multides em estdios lotados entoando 'MARIO! MARIO!', culminando na imagem "
    L"da icnica caixa amarela com Mario alando voo. [6]\n\n"
    L"A revista Nintendo Power dedicou capas, estratgias completas e encartes colecionveis. "
    L"Filas gigantescas se formaram nas lojas americanas, consolidando o ttulo como febre global.",
    L"[6] The Wizard (1989) / AFI Catalog",
    L"https://catalog.afi.com/Catalog/moviedetails/58364",
    L"Nintendo Power Archives",
    L"https://archive.org/details/Nintendo_Power_Issue011_March-April_1990"
},
{
    L"Diferenas Regionais e Revises",
    L"Famicom vs NES PRG0 vs Reviso 1 (Rev 1)",
    L"Existem diferenas marcantes entre as verses de lanamento. No Famicom japons original (1988), levar "
    L"dano enquanto Mario veste qualquer traje especial (Frog, Tanooki, Hammer ou Raccoon) reverte imediatamente "
    L"para o Mario pequeno, tornando o jogo muito mais impiedoso. [4]\n\n"
    L"Na verso ocidental de NES (PRG0 e a subsequente Rev 1), a Nintendo alterou o balanceamento para que sofrer dano "
    L"reverta para o Super Mario (grande), tornando a experincia mais equilibrada e divertida. [4]\n\n"
    L"A Reviso 1 (Rev 1) presente neste projeto tambm corrigiu glitches de coliso crticos presentes na verso "
    L"PRG0 (como o risco de ficar preso na parede no final da fase 5-1) e refinou a traduo de nomes de reinos "
    L"e itens. Este recompilador utiliza e valida exatamente a cpia oficial 'USA Rev 1'. [4]",
    L"[4] TCRF - SMB3 Regional & Version Differences",
    L"https://tcrf.net/Super_Mario_Bros._3/Version_Differences",
    L"Speedrun & Glitch Document",
    L"https://www.speedrun.com/smb3"
},
{
    L"Vendas e Legado Eterno",
    L"Mais de 17 milhes de cartuchos e aclamao unnime",
    L"Super Mario Bros. 3 vendeu mais de 17,28 milhes de cartuchos individuais (sem contar unidades inclusas "
    L"em pacotes de console), gerando mais de meio bilho de dlares em receitas no incio dos anos 1990, "
    L"superando na poca as maiores bilheterias de Hollywood. [7]\n\n"
    L"O ttulo figura de forma consistente em posies de honra nas listas de 'Melhores Jogos de Todos os Tempos' "
    L"de publicaes especializadas como EGM, IGN, Edge, Famitsu e GameSpot. [7]\n\n"
    L"Sua influncia moldou todas as geraes posteriores: Super Mario World herdou os mapas abertos e o voo; "
    L"Super Mario 3D Land resgatou o Raccoon e a Bota; e a expressividade visual dos Koopalings tornou-se "
    L"parte definitiva da identidade da Nintendo.",
    L"[7] Game Developer / Guinness World Records",
    L"https://www.guinnessworldrecords.com/world-records/best-selling-nes-game",
    L"IGN Best Games of All Time",
    L"https://www.ign.com/lists/top-100-games"
},
{
    L"Fontes e Metodologia",
    L"Pesquisa consultada e referncias tcnicas em 2026",
    L"[1] Iwata Asks: New Super Mario Bros. Wii - Volume 2: The History of 2D Mario Games (Nintendo Co., Ltd.).\n"
    L"[2] Super Mario Bros. 3 Archive & Database: Super Mario Wiki.\n"
    L"[3] Nesdev Wiki: MMC3 Mapper 4 Architecture & PPU A12 IRQ counter specifications.\n"
    L"[4] The Cutting Room Floor (TCRF): Super Mario Bros. 3 disassembly, unused content and revision history.\n"
    L"[5] Koji Kondo Sound Interviews: Nintendo Official Commemorative Archive.\n"
    L"[6] American Film Institute (AFI) Catalog & Nintendo Power Issue 11 (March/April 1990).\n"
    L"[7] Guinness World Records & Videogame Sales Archive.\n\n"
    L"Todos os textos editoriais so de autoria prpria deste projeto. Todas as marcas registradas, nomes de "
    L"personagens, imagens e cdigos de jogo originais pertencem  Nintendo Co., Ltd.",
    L"[3] Nesdev Wiki",
    L"https://www.nesdev.org/wiki/MMC3",
    L"[4] TCRF Research Archive",
    L"https://tcrf.net/Super_Mario_Bros._3"
}
};

#define HISTORY_COUNT ((int)(sizeof(history_chapters) / sizeof(history_chapters[0])))
