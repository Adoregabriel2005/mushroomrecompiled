#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "preferences.h"
#include "choice.h"
#include "history.h"
#include "theme.h"
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uxtheme.h>

static int history_page = 0;
static HWND history_choice;
static HWND nav[6];
static DWORD page_started;
static int closing = 0;
static HWND dialog = NULL, owner_window = NULL, tabs_control = NULL;
static HWND items[128];
static HWND key_buttons[11];
static HWND pad_combos[10];
static HWND device_combo, deadzone_combo;
static HWND video_controls[10], audio_controls[8];
static Preferences pending, original, *target_prefs;
static InputDevice devices[32];
static int device_count = 0, item_count = 0, current_tab = 0;
static int capture_key = -1, capture_pad = -1;
static HBRUSH bg_brush = NULL;
static HFONT normal_font = NULL, heading_font = NULL, caption_font = NULL, widget_font = NULL;
static wchar_t asset_directory[32768];

typedef struct {
    uint32_t *pixels;
    UINT w, h;
    BITMAPINFO info;
} Picture;

static Picture boxart, advert;
static wchar_t feedback_msg[200] = {0};

static const wchar_t *action_labels[] = {
    L"Cima", L"Baixo", L"Esquerda", L"Direita",
    L"Boto B (Correr/Cauda)", L"Boto A (Pular)",
    L"Select (Itens)", L"Start (Pause)",
    L"Turbo B", L"Turbo A", L"Menu Principal"
};

static const wchar_t *pad_action_labels[] = {
    L"Boto B (Ataque/Corrida)", L"Boto A (Pulo)",
    L"Select", L"Start",
    L"D-Pad Cima", L"D-Pad Baixo", L"D-Pad Esquerda", L"D-Pad Direita",
    L"Turbo B", L"Turbo A"
};

static int clamp_val(int v, int low, int high) {
    return v < low ? low : v > high ? high : v;
}

static void read_int(const wchar_t *section, const wchar_t *name, int *v, int low, int high, const wchar_t *path) {
    *v = clamp_val(GetPrivateProfileIntW(section, name, *v, path), low, high);
}

static void write_int(const wchar_t *s, const wchar_t *k, int v, const wchar_t *path) {
    wchar_t value[32];
    swprintf(value, 32, L"%d", v);
    WritePrivateProfileStringW(s, k, value, path);
}

void preferences_defaults(Preferences *p) {
    memset(p, 0, sizeof *p);
    int initial_keys[] = {
        VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
        'Z', 'X', VK_SHIFT, VK_RETURN,
        'A', 'S', VK_ESCAPE
    };
    memcpy(p->keys, initial_keys, sizeof initial_keys);
    for (int i = 0; i < 11; i++) p->buttons[i] = -1;
    p->deadzone = 25;
    p->resolution = 2;       /* 768x720 default (3x) */
    p->aspect = 2;           /* wide phase, original HUD */
    p->scale = 0;            /* Fit */
    p->filter = 0;           /* Pixel Art / Crisp */
    p->scanlines = 0;        /* Off */
    p->brightness = 100;
    p->fullscreen = 0;
    p->volume = 100;
    p->sample_rate = 48000;
    p->stereo = 1;           /* Stereo panning on by default */
    p->latency = 1;          /* Balanceada ~20ms */
    wcscpy(p->audio_device, L"Padro do Sistema");
}

void preferences_load(Preferences *p, const wchar_t *path) {
    preferences_defaults(p);
    wchar_t key[32], device[512];
    for (int i = 0; i < 11; i++) {
        swprintf(key, 32, L"key%d", i);
        read_int(L"input", key, &p->keys[i], 1, 254, path);
    }
    for (int i = 0; i < 11; i++) {
        swprintf(key, 32, L"button%d", i);
        read_int(L"input", key, &p->buttons[i], -1, 255, path);
    }
    GetPrivateProfileStringW(L"input", L"device", L"", device, 512, path);
    WideCharToMultiByte(CP_UTF8, 0, device, -1, p->device, 512, NULL, NULL);
    read_int(L"input", L"deadzone", &p->deadzone, 5, 80, path);

    read_int(L"video", L"resolution", &p->resolution, 0, 4, path);
    read_int(L"video", L"aspect", &p->aspect, 0, 2, path);
    read_int(L"video", L"scale", &p->scale, 0, 2, path);
    read_int(L"video", L"filter", &p->filter, 0, 1, path);
    read_int(L"video", L"scanlines", &p->scanlines, 0, 2, path);
    read_int(L"video", L"brightness", &p->brightness, 50, 150, path);
    read_int(L"video", L"fullscreen", &p->fullscreen, 0, 1, path);

    read_int(L"audio", L"volume", &p->volume, 0, 100, path);
    read_int(L"audio", L"muted", &p->muted, 0, 1, path);
    read_int(L"audio", L"sample_rate", &p->sample_rate, 44100, 96000, path);
    if (p->sample_rate != 44100 && p->sample_rate != 48000 && p->sample_rate != 96000) p->sample_rate = 48000;
    read_int(L"audio", L"stereo", &p->stereo, 0, 1, path);
    read_int(L"audio", L"latency", &p->latency, 0, 2, path);
    GetPrivateProfileStringW(L"audio", L"device", L"Padro do Sistema", p->audio_device, 128, path);
}

void preferences_save(const Preferences *p, const wchar_t *path) {
    wchar_t k[32], device[512];
    for (int i = 0; i < 11; i++) {
        swprintf(k, 32, L"key%d", i);
        write_int(L"input", k, p->keys[i], path);
    }
    for (int i = 0; i < 11; i++) {
        swprintf(k, 32, L"button%d", i);
        write_int(L"input", k, p->buttons[i], path);
    }
    MultiByteToWideChar(CP_UTF8, 0, p->device, -1, device, 512);
    WritePrivateProfileStringW(L"input", L"device", device, path);
    write_int(L"input", L"deadzone", p->deadzone, path);

    write_int(L"video", L"resolution", p->resolution, path);
    write_int(L"video", L"aspect", p->aspect, path);
    write_int(L"video", L"scale", p->scale, path);
    write_int(L"video", L"filter", p->filter, path);
    write_int(L"video", L"scanlines", p->scanlines, path);
    write_int(L"video", L"brightness", p->brightness, path);
    write_int(L"video", L"fullscreen", p->fullscreen, path);

    write_int(L"audio", L"volume", p->volume, path);
    write_int(L"audio", L"muted", p->muted, path);
    write_int(L"audio", L"sample_rate", p->sample_rate, path);
    write_int(L"audio", L"stereo", p->stereo, path);
    write_int(L"audio", L"latency", p->latency, path);
    WritePrivateProfileStringW(L"audio", L"device", p->audio_device, path);
}

RECT video_destination(int w, int h, int aspect, int scale, RECT *source) {
    RECT dst = {0, 0, 0, 0};
    *source = (RECT){0, 0, 256, 240};
    if (w <= 0 || h <= 0) return dst;

    /* Native aspect modes. The extended 16:9 canvas is fitted by app.c. */
    int an = (aspect == 1) ? 16 : (aspect == 2) ? 16 : 4;
    int ad = (aspect == 1) ? 15 : (aspect == 2) ? 9  : 3;

    int cw = w, ch = MulDiv(w, ad, an);
    if (ch > h) { ch = h; cw = MulDiv(h, an, ad); }
    int ox = (w - cw) / 2, oy = (h - ch) / 2;

    int dw = cw, dh = ch;
    if (scale == 2) {
        /* Crop & Fill */
        if ((int64_t)cw * 240 > (int64_t)ch * 256) {
            int crop = MulDiv(256, ch, cw);
            source->top = (240 - crop) / 2;
            source->bottom = source->top + crop;
        } else {
            int crop = MulDiv(240, cw, ch);
            source->left = (256 - crop) / 2;
            source->right = source->left + crop;
        }
    } else if (scale == 1 && cw >= 256 && ch >= 240) {
        /* Pixel Perfect Integer Scale */
        int k = cw / 256;
        if (ch / 240 < k) k = ch / 240;
        if (k < 1) k = 1;
        dw = 256 * k;
        dh = 240 * k;
        ox += (cw - dw) / 2;
        oy += (ch - dh) / 2;
    }
    dst = (RECT){ox, oy, ox + dw, oy + dh};
    return dst;
}


static HWND fields[40], page_nav[7], live_label;
static int capture_wait_release, previous_pad;
static HBRUSH panel_brush;
static WNDPROC unused_proc;
static const wchar_t *pages[]={L"Teclado",L"Controle",L"Vídeo",L"Áudio",L"O jogo",L"Projeto",L"Créditos"};
static const wchar_t *key_names[]={L"Cima",L"Baixo",L"Esquerda",L"Direita",L"B · correr / cauda",L"A · pular / voar",L"Select · itens",L"Start · pausa NES",L"Turbo B",L"Turbo A",L"Menu do PC"};
static const wchar_t *pad_names[]={L"B · correr / cauda",L"A · pular / voar",L"Select · itens",L"Start · pausa NES",L"Cima",L"Baixo",L"Esquerda",L"Direita",L"Turbo B",L"Turbo A",L"Menu do PC"};
static HWND add_control(const wchar_t *type,const wchar_t *text,int id,int x,int y,int w,int h,DWORD flags) {
 RECT r; GetClientRect(dialog,&r);
 if(!wcscmp(type,L"BUTTON"))flags=BS_OWNERDRAW;
 HWND c=CreateWindowW(type,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|flags,MulDiv(x,r.right,1100),MulDiv(y,r.bottom,720),MulDiv(w,r.right,1100),MulDiv(h,r.bottom,720),dialog,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
 SendMessageW(c,WM_SETFONT,(WPARAM)normal_font,TRUE);
 if(item_count<128)items[item_count++]=c;
 return c;
}
static HWND label(const wchar_t *s,int x,int y,int w) { return add_control(L"STATIC",s,0,x,y,w,30,SS_LEFT); }
static HWND option(int id,int x,int y,int w,const wchar_t *values[],int n,int selected) {
 HWND c=add_control(L"Smb3Choice",L"",id,x,y,w,32,0);
 for(int i=0;i<n;i++)SendMessageW(c,CB_ADDSTRING,0,(LPARAM)values[i]);
 SendMessageW(c,CB_SETCURSEL,selected,0); return c;
}
static void key_text(int i) {
 wchar_t text[64]; UINT scan=MapVirtualKeyW(pending.keys[i],MAPVK_VK_TO_VSC);
 if(pending.keys[i]>=VK_LEFT && pending.keys[i]<=VK_DOWN)scan|=0x100;
 if(!GetKeyNameTextW((LONG)(scan<<16),text,64))swprintf(text,64,L"Tecla %d",pending.keys[i]);
 SetWindowTextW(key_buttons[i],text);
}
static void build_page(void) {
 for(int i=0;i<item_count;i++)DestroyWindow(items[i]); item_count=0;
 memset(fields,0,sizeof fields); capture_key=capture_pad=-1;
 for(int i=0;i<7;i++)page_nav[i]=add_control(L"BUTTON",pages[i],200+i,30+i*149,22,143,38,BS_PUSHBUTTON);
 add_control(L"BUTTON",L"Aplicar e voltar",900,830,659,235,38,BS_DEFPUSHBUTTON);
 add_control(L"BUTTON",L"Descartar",901,665,659,150,38,BS_PUSHBUTTON);
 add_control(L"BUTTON",L"Restaurar esta página",902,35,659,250,38,BS_PUSHBUTTON);
 if(current_tab==0) {
  label(L"Clique na ação e pressione a nova tecla. Esc cancela a captura.",35,125,1000);
  for(int i=0;i<11;i++){int col=i/6,row=i%6; label(key_names[i],35+col*535,180+row*62,240); key_buttons[i]=add_control(L"BUTTON",L"",100+i,285+col*535,174+row*62,230,38,BS_PUSHBUTTON); key_text(i);}
 } else if(current_tab==1) {
  device_count=input_devices(devices,32); const wchar_t *names[33];names[0]=L"Automático · primeiro controle conectado";int selected=0;
  for(int i=0;i<device_count;i++){names[i+1]=devices[i].name;if(!strcmp(pending.device,devices[i].key))selected=i+1;}
  fields[0]=option(300,35,125,720,names,device_count+1,selected);
  add_control(L"BUTTON",L"Atualizar dispositivos",311,780,125,280,32,BS_PUSHBUTTON);
  const wchar_t *zones[]={L"5%",L"15%",L"25%",L"35%",L"50%"};
  label(L"Zona morta",35,175,190);fields[1]=option(301,245,170,200,zones,5,pending.deadzone<=5?0:pending.deadzone<=15?1:pending.deadzone<=25?2:pending.deadzone<=35?3:4);
  label(L"USB / Bluetooth / receptor · XInput, DualShock, DualSense, Switch e genéricos via SDL",35,214,1020);
  for(int i=0;i<11;i++){int col=i/6,row=i%6;label(pad_names[i],35+col*535,265+row*47,240);wchar_t text[100];if(pending.buttons[i]<0)wcscpy(text,L"Automático");else swprintf(text,100,L"Botão físico %d",pending.buttons[i]);pad_combos[i]=add_control(L"BUTTON",text,400+i,285+col*535,258+row*47,230,34,BS_PUSHBUTTON);}
  live_label=label(input_name(),35,566,1010);
  label(L"Aperte um botão para testar. Home/Guide abre o menu; Select continua disponível no NES.",35,605,1020);
 } else if(current_tab==2) {
  const wchar_t *aspect[]={L"4:3 · televisão",L"Pixels quadrados · 16:15",L"16:9 · fase ampliada / HUD original"};
  const wchar_t *scale[]={L"Ajustar à janela",L"Escala inteira",L"Recortar e preencher"};
  const wchar_t *filter[]={L"Nítido · nearest",L"Suave · bilinear"};
  const wchar_t *scan[]={L"Desligadas",L"Suaves",L"Marcadas"};
  const wchar_t *sizes[]={L"960 × 720",L"1024 × 768",L"1280 × 720",L"1600 × 900",L"1920 × 1080"};
  const wchar_t *mode[]={L"Janela",L"Tela cheia sem bordas"};
  const wchar_t *bright[]={L"50%",L"75%",L"100%",L"125%",L"150%"};
  const wchar_t *labels[]={L"Proporção",L"Escala",L"Filtro",L"Scanlines",L"Resolução da janela",L"Exibição",L"Brilho"};
  for(int i=0;i<7;i++)label(labels[i],45,140+i*62,320);
  fields[0]=option(320,400,133,600,aspect,3,pending.aspect); fields[1]=option(321,400,195,600,scale,3,pending.scale);
  fields[2]=option(322,400,257,600,filter,2,pending.filter);fields[3]=option(323,400,319,600,scan,3,pending.scanlines);
  fields[4]=option(324,400,381,600,sizes,5,pending.resolution);fields[5]=option(325,400,443,600,mode,2,pending.fullscreen);fields[6]=option(326,400,505,600,bright,5,(pending.brightness-50)/25);
  label(L"16:9 amplia o cenário nas fases horizontais. HUD centralizado, com barras pretas.",45,585,1000);
  label(L"Experimental: objetos ativos na área ampliada; salas verticais mantêm a largura original.",45,615,1000);
 } else if(current_tab==3) {
  const wchar_t *vol[]={L"0%",L"10%",L"20%",L"30%",L"40%",L"50%",L"60%",L"70%",L"80%",L"90%",L"100%"};
  const wchar_t *rates[]={L"44.100 Hz",L"48.000 Hz",L"96.000 Hz"}; const wchar_t *muted[]={L"Som ligado",L"Silenciar"};
  const wchar_t *latency[]={L"Baixa · 20 ms",L"Equilibrada · 40 ms",L"Estável · 60 ms"};
  const wchar_t *labels[]={L"Volume",L"Frequência",L"Saída",L"Reserva de áudio",L"Dispositivo"};
  for(int i=0;i<5;i++)label(labels[i],45,140+i*78,320);
  fields[0]=option(340,400,133,600,vol,11,pending.volume/10);fields[1]=option(341,400,211,600,rates,3,pending.sample_rate==44100?0:pending.sample_rate==96000?2:1);
  fields[2]=option(342,400,289,600,muted,2,pending.muted);fields[3]=option(343,400,367,600,latency,3,pending.latency);
  const wchar_t *names[65]; wchar_t device_names[64][128];names[0]=L"Padrão do sistema"; unsigned n=waveOutGetNumDevs();if(n>64)n=64;int selected=0;
  for(unsigned i=0;i<n;i++){WAVEOUTCAPSW caps;waveOutGetDevCapsW(i,&caps,sizeof caps);wcsncpy(device_names[i],caps.szPname,127);names[i+1]=device_names[i];if(!wcscmp(pending.audio_device,names[i+1]))selected=i+1;}
  fields[4]=option(344,400,445,600,names,n+1,selected);
  label(L"Áudio original mono do NES, reproduzido nos dois canais. Aumente a reserva se houver cortes.",45,555,1000);
 } else {
  const wchar_t *text=current_tab==4 ? L"SUPER MARIO BROS. 3\n\nFamicom · 1988 / NES · 1990\nNintendo R&D4\n\nMario e Luigi atravessam oito mundos para recuperar as varinhas dos reis e enfrentar Bowser e os Koopalings.\n\nSuper Leaf, Tanooki Suit, Frog Suit e Hammer Suit ampliam as formas de explorar as fases. No mapa, Select abre os itens; Start inicia ou pausa a partida.\n\nArtes originais da época: Nintendo. Acervo consultado: Super Mario Wiki, galeria de Super Mario Bros. 3." : current_tab==5 ? L"O PROJETO\n\nPort para Windows usando recompilação estática do código 6502 e o runtime cycle-accurate do NESRecomp.\n\nA seleção de ROM exige Super Mario Bros. 3 (USA) (Rev 1). A identificação é verificada antes de habilitar Jogar.\n\nAs configurações são salvas localmente. Salvar e carregar preservam o estado completo da máquina.\n\nReferência de experiência e organização: Mônica no Castelo do Dragão Recomp, de Adoregabriel2005.\n\nProjeto de fã, independente e não oficial. Validação automatizada não comprova todas as fases e dispositivos." : L"CRÉDITOS\n\nJogo, personagens, ilustrações e música: Nintendo.\nDireção: Takashi Tezuka. Produção: Shigeru Miyamoto. Música: Koji Kondo.\n\nBase técnica: NESRecomp / Matthew Stanley e colaboradores.\nControles e banco de mapeamentos: SDL2 e SDL_GameControllerDB (zlib).\nInterface de referência: Adoregabriel2005 / Mônica Recomp.\n\nBeta testers: ALEISTECH e RETROCESSO.\n\nAs ilustrações originais pertencem à Nintendo. As fontes e os arquivos utilizados estão documentados em docs/ARTES.md.\n\nEste projeto não é afiliado, patrocinado ou aprovado pela Nintendo.";
  add_control(L"STATIC",text,0,45,125,current_tab==4?610:990,485,SS_LEFT);
  add_control(L"BUTTON",current_tab==4?L"Ver acervo de artes e fontes":L"Abrir documentação local",950,45,605,500,32,BS_PUSHBUTTON);
 }
 InvalidateRect(dialog,NULL,TRUE);
}
static void collect_page(void) {
 if(current_tab==1){int i=SendMessageW(fields[0],CB_GETCURSEL,0,0);snprintf(pending.device,sizeof pending.device,"%s",i>0&&i<=device_count?devices[i-1].key:"");int zones[]={5,15,25,35,50};pending.deadzone=zones[SendMessageW(fields[1],CB_GETCURSEL,0,0)];}
 if(current_tab==2){int *p[]={&pending.aspect,&pending.scale,&pending.filter,&pending.scanlines,&pending.resolution,&pending.fullscreen};for(int i=0;i<6;i++)*p[i]=SendMessageW(fields[i],CB_GETCURSEL,0,0);pending.brightness=50+25*SendMessageW(fields[6],CB_GETCURSEL,0,0);}
 if(current_tab==3){pending.volume=10*SendMessageW(fields[0],CB_GETCURSEL,0,0);int rates[]={44100,48000,96000};pending.sample_rate=rates[SendMessageW(fields[1],CB_GETCURSEL,0,0)];pending.muted=SendMessageW(fields[2],CB_GETCURSEL,0,0);pending.latency=SendMessageW(fields[3],CB_GETCURSEL,0,0);int n=SendMessageW(fields[4],CB_GETCURSEL,0,0);if(n>0)SendMessageW(fields[4],CB_GETLBTEXT,n,(LPARAM)pending.audio_device);else pending.audio_device[0]=0;}
}
static LRESULT CALLBACK settings_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
 if(msg==WM_DRAWITEM){DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;int active=(d->CtlID==200+(unsigned)current_tab)||(d->itemState&ODS_FOCUS);int primary=d->CtlID==900;HBRUSH b=CreateSolidBrush(primary?RGB(182,38,43):active?RGB(40,59,85):RGB(25,40,61));FillRect(d->hDC,&d->rcItem,b);DeleteObject(b);HPEN pen=CreatePen(PS_SOLID,1,active||primary?RGB(255,209,63):RGB(66,88,113));HGDIOBJ op=SelectObject(d->hDC,pen),ob=SelectObject(d->hDC,GetStockObject(NULL_BRUSH));Rectangle(d->hDC,0,0,d->rcItem.right,d->rcItem.bottom);SelectObject(d->hDC,op);SelectObject(d->hDC,ob);DeleteObject(pen);wchar_t text[200];GetWindowTextW(d->hwndItem,text,200);HFONT of=SelectObject(d->hDC,normal_font);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,active||primary?RGB(255,216,74):RGB(235,238,243));DrawTextW(d->hDC,text,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(d->hDC,of);return TRUE;}
 if(msg==WM_COMMAND){int id=LOWORD(wp);
  if(id>=200&&id<207){collect_page();current_tab=id-200;build_page();return 0;}
  if(id==900){collect_page();*target_prefs=pending;DestroyWindow(hwnd);SendMessageW(owner_window,PREFS_APPLIED,0,0);return 0;}
  if(id==901){DestroyWindow(hwnd);return 0;}
  if(id==902){Preferences defaults;preferences_defaults(&defaults);if(current_tab==0)memcpy(pending.keys,defaults.keys,sizeof pending.keys);else if(current_tab==1){memcpy(pending.buttons,defaults.buttons,sizeof pending.buttons);pending.deadzone=25;pending.device[0]=0;}else if(current_tab==2){pending.aspect=2;pending.scale=0;pending.filter=0;pending.scanlines=0;pending.brightness=100;pending.resolution=2;pending.fullscreen=0;}else if(current_tab==3){pending.volume=100;pending.muted=0;pending.sample_rate=48000;pending.latency=1;pending.audio_device[0]=0;}build_page();return 0;}
  if(id==311){collect_page();input_select(pending.device);input_update();build_page();return 0;}
  if(id==300){collect_page();input_select(pending.device);input_update();return 0;}
  if(id>=100&&id<111){capture_key=id-100;SetWindowTextW(key_buttons[capture_key],L"Pressione uma tecla…");SetFocus(hwnd);return 0;}
  if(id>=400&&id<411){capture_pad=id-400;capture_wait_release=1;SetWindowTextW(pad_combos[capture_pad],L"Solte e aperte o botão…");return 0;}
  if(id==950){ShellExecuteW(hwnd,L"open",current_tab==4?L"https://www.mariowiki.com/Gallery:Super_Mario_Bros._3":L"docs",NULL,NULL,SW_SHOWNORMAL);return 0;}
 }
 if(msg==WM_KEYDOWN){if(capture_key>=0){int i=capture_key;if(wp!=VK_ESCAPE)pending.keys[i]=(int)wp;capture_key=-1;key_text(i);return 0;}if(wp==VK_ESCAPE){DestroyWindow(hwnd);return 0;}}
 if(msg==WM_TIMER){input_update();if(current_tab==1){int b=input_first_pressed();if(capture_pad>=0){if(b<0)capture_wait_release=0;else if(!capture_wait_release){pending.buttons[capture_pad]=b;wchar_t t[64];swprintf(t,64,L"Botão físico %d",b);SetWindowTextW(pad_combos[capture_pad],t);capture_pad=-1;}}wchar_t live[256];swprintf(live,256,L"%ls   ·   %ls %d",input_name(),b<0?L"Nenhum botão pressionado":L"Botão",b);SetWindowTextW(live_label,live);}
  InputState s=input_read();int edge=s.pad&~previous_pad;previous_pad=s.pad;if(capture_pad<0&&capture_key<0){HWND current_focus=GetFocus();wchar_t focus_type[40];GetClassNameW(current_focus,focus_type,40);int is_choice=!wcscmp(focus_type,L"Smb3Choice");if(edge&INPUT_LEFT){if(is_choice)SendMessageW(current_focus,WM_KEYDOWN,VK_LEFT,0);else{collect_page();current_tab=(current_tab+6)%7;build_page();}}if(edge&INPUT_RIGHT){if(is_choice)SendMessageW(current_focus,WM_KEYDOWN,VK_RIGHT,0);else{collect_page();current_tab=(current_tab+1)%7;build_page();}}if(edge&INPUT_B){DestroyWindow(hwnd);return 0;}if(edge&INPUT_DOWN)SetFocus(GetNextDlgTabItem(hwnd,GetFocus(),FALSE));if(edge&INPUT_UP)SetFocus(GetNextDlgTabItem(hwnd,GetFocus(),TRUE));if(edge&INPUT_A){HWND focus=GetFocus();wchar_t name[40];GetClassNameW(focus,name,40);if(!wcscmp(name,L"BUTTON"))SendMessageW(focus,BM_CLICK,0,0);else SendMessageW(focus,WM_KEYDOWN,VK_RIGHT,0);}}return 0;}
 if(msg==WM_SIZE){if(dialog){collect_page();build_page();}return 0;}
 if(msg==WM_CTLCOLORSTATIC){SetTextColor((HDC)wp,RGB(239,238,220));SetBkColor((HDC)wp,RGB(17,27,45));return (LRESULT)panel_brush;}
 if(msg==WM_ERASEBKGND)return 1;
 if(msg==WM_PAINT||msg==WM_PRINTCLIENT){PAINTSTRUCT ps;HDC dc=msg==WM_PRINTCLIENT?(HDC)wp:BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);FillRect(dc,&r,panel_brush);HFONT old=SelectObject(dc,heading_font);SetTextColor(dc,RGB(255,209,63));SetBkMode(dc,TRANSPARENT);RECT title={35,MulDiv(80,r.bottom,720),r.right-35,MulDiv(116,r.bottom,720)};DrawTextW(dc,pages[current_tab],-1,&title,DT_LEFT|DT_VCENTER|DT_SINGLELINE);if(current_tab==4){RECT art={MulDiv(700,r.right,1100),MulDiv(145,r.bottom,720),MulDiv(1060,r.right,1100),MulDiv(580,r.bottom,720)};theme_gallery(dc,art);}SelectObject(dc,old);if(msg==WM_PAINT)EndPaint(hwnd,&ps);return 0;}
 if(msg==WM_DESTROY){KillTimer(hwnd,1);dialog=NULL;item_count=0;SendMessageW(owner_window,PREFS_CLOSED,0,0);if(normal_font)DeleteObject(normal_font);if(heading_font)DeleteObject(heading_font);if(panel_brush)DeleteObject(panel_brush);return 0;}
 return DefWindowProcW(hwnd,msg,wp,lp);
}
int preferences_active(void){return dialog!=NULL;}
void preferences_open(HWND parent,Preferences *p,int tab,const wchar_t *assets){
 if(dialog)return;owner_window=parent;target_prefs=p;pending=*p;original=*p;current_tab=tab;item_count=0;
 normal_font=CreateFontW(-17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");heading_font=CreateFontW(-25,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");panel_brush=CreateSolidBrush(RGB(17,27,45));
 WNDCLASSW wc={0};wc.lpfnWndProc=settings_proc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"Smb3SettingsPage";wc.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClassW(&wc);
 RECT r;GetClientRect(parent,&r);dialog=CreateWindowW(wc.lpszClassName,L"Configurações",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,r.right,r.bottom,parent,NULL,wc.hInstance,NULL);build_page();SetTimer(dialog,1,40,NULL);SetFocus(dialog);
}
int preferences_route(MSG *msg){if(!dialog)return 0;if(msg->hwnd==dialog||IsChild(dialog,msg->hwnd)){if(msg->message==WM_KEYDOWN&&(capture_key>=0||msg->wParam==VK_ESCAPE)){SendMessageW(dialog,WM_KEYDOWN,msg->wParam,msg->lParam);return 1;}if(IsDialogMessageW(dialog,msg))return 1;TranslateMessage(msg);DispatchMessageW(msg);return 1;}return 0;}
void preferences_resize(void){if(dialog){RECT r;GetClientRect(owner_window,&r);MoveWindow(dialog,0,0,r.right,r.bottom,TRUE);}}
