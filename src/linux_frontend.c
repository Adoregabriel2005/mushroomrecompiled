/* Experimental SDL2 host. Game code is generated locally, never embedded here. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "glue.h"
#include "frame_clock.h"

static SDL_GameController *controller;
static void open_controller(void) {
    if (controller && !SDL_GameControllerGetAttached(controller)) {
        SDL_GameControllerClose(controller); controller = NULL;
    }
    if (!controller) for (int i=0;i<SDL_NumJoysticks();i++) {
        if (SDL_IsGameController(i) && (controller=SDL_GameControllerOpen(i))) {
            fprintf(stderr,"Controller: %s\n",SDL_GameControllerName(controller)); break;
        }
    }
}
static int button(SDL_GameControllerButton b) {
    return controller && SDL_GameControllerGetButton(controller,b);
}
static uint8_t read_pad(void) {
    const Uint8 *k=SDL_GetKeyboardState(NULL);
    int x=controller?SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTX):0;
    int y=controller?SDL_GameControllerGetAxis(controller,SDL_CONTROLLER_AXIS_LEFTY):0;
    uint8_t p=0;
    if (k[SDL_SCANCODE_X] || button(SDL_CONTROLLER_BUTTON_A) || button(SDL_CONTROLLER_BUTTON_Y)) p|=NES_PAD_A;
    if (k[SDL_SCANCODE_Z] || button(SDL_CONTROLLER_BUTTON_B) || button(SDL_CONTROLLER_BUTTON_X)) p|=NES_PAD_B;
    if (k[SDL_SCANCODE_RETURN] || button(SDL_CONTROLLER_BUTTON_START)) p|=NES_PAD_START;
    if (k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_LSHIFT] || button(SDL_CONTROLLER_BUTTON_LEFTSTICK)) p|=NES_PAD_SELECT;
    if (k[SDL_SCANCODE_UP] || y < -8000 || button(SDL_CONTROLLER_BUTTON_DPAD_UP)) p|=NES_PAD_UP;
    if (k[SDL_SCANCODE_DOWN] || y > 8000 || button(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) p|=NES_PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT] || x < -8000 || button(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) p|=NES_PAD_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || x > 8000 || button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) p|=NES_PAD_RIGHT;
    if ((p & 3)==3) p&=~3;
    if ((p & 12)==12) p&=~12;
    return p;
}
static void usage(void) {
    puts("Mushroom Recompiled - experimental Linux frontend\n"
         "Usage: MushroomRecompiled --rom game.nes [--fullscreen] [--aspect 4:3|16:9]\n"
         "       [--fps 30|40|60|0] [--perf] [--headless] [--seconds N]\n"
         "       [--present-delay-ms N] (diagnostic presentation stall)\n"
         "Esc/Back pause; X/A jump; Z/B/X run; Enter/Start NES pause; Shift/L3 select\n"
         "F5/R1 quicksave; F9/L1 quickload; F10 aspect; F11 fullscreen; Ctrl+Q quit");
}
int main(int argc,char **argv) {
    const char *rom=NULL; int fullscreen=0,wide=1,fps=60,perf=0,headless=0,delay=0;
    double seconds=0;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--help")) { usage(); return 0; }
        else if (!strcmp(argv[i],"--rom") && i+1<argc) rom=argv[++i];
        else if (!strcmp(argv[i],"--fullscreen")) fullscreen=1;
        else if (!strcmp(argv[i],"--windowed")) fullscreen=0;
        else if (!strcmp(argv[i],"--perf")) perf=1;
        else if (!strcmp(argv[i],"--headless")) headless=1;
        else if (!strcmp(argv[i],"--aspect") && i+1<argc) {
            const char *v=argv[++i];
            if (strcmp(v,"4:3") && strcmp(v,"16:9")) { usage(); return 2; }
            wide=!strcmp(v,"16:9");
        } else if (!strcmp(argv[i],"--fps") && i+1<argc) {
            const char *v=argv[++i];
            if (strcmp(v,"0") && strcmp(v,"30") && strcmp(v,"40") && strcmp(v,"60")) { usage(); return 2; }
            fps=atoi(v);
        } else if ((!strcmp(argv[i],"--seconds") || !strcmp(argv[i],"--present-delay-ms")) && i+1<argc) {
            int is_delay=!strcmp(argv[i],"--present-delay-ms"); char *end;
            double v=strtod(argv[++i],&end);
            if (*end || !isfinite(v) || v<0 || v>86400) { usage(); return 2; }
            if (is_delay) delay=(int)fmin(v,10000); else seconds=v;
        } else if (argv[i][0]!='-' && !rom) rom=argv[i];
        else { usage(); return 2; }
    }
    if (!rom) { usage(); return 2; }
    if (headless) { SDL_setenv("SDL_VIDEODRIVER","dummy",0); SDL_setenv("SDL_AUDIODRIVER","dummy",0); }
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)) {
        fprintf(stderr,"SDL_Init: %s\n",SDL_GetError()); return 1;
    }
    int result=1; SDL_Window *window=NULL; SDL_Renderer *renderer=NULL; SDL_Texture *texture=NULL;
    SDL_AudioDeviceID audio=0; char *pref=SDL_GetPrefPath("Underplay","MushroomRecompiled");
    char *save=NULL; char *base=SDL_GetBasePath();
    if (base) {
        char *db=malloc(strlen(base)+40);
        if (db) { sprintf(db,"%sassets/gamecontrollerdb.txt",base); SDL_GameControllerAddMappingsFromFile(db); free(db); }
        SDL_free(base);
    }
    if (!pref || !(save=malloc(strlen(pref)+32))) goto done;
    sprintf(save,"%squickstate.save",pref);
    if (!glue_load_rom(rom)) { fprintf(stderr,"Incompatible or unreadable local ROM. Expected USA PRG1 payload CRC32 2e6301ed.\n"); goto done; }
    glue_set_widescreen(wide);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
    window=SDL_CreateWindow("Mushroom Recompiled",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,
        SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|(headless?SDL_WINDOW_HIDDEN:0)|(fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0));
    if (!window) goto sdl_error;
    renderer=SDL_CreateRenderer(window,-1,headless?SDL_RENDERER_SOFTWARE:SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if (!renderer) goto sdl_error;
    texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,426,240);
    if (!texture) goto sdl_error;
    SDL_AudioSpec want={0},got;
    want.freq=48000; want.format=AUDIO_S16SYS; want.channels=2; want.samples=1024;
    audio=SDL_OpenAudioDevice(NULL,0,&want,&got,0);
    if (!audio) { fprintf(stderr,"Audio unavailable: %s (continuing silently)\n",SDL_GetError()); }
    else SDL_PauseAudioDevice(audio,0);
    open_controller();
    usage();
    FrameClock clock; frame_clock_init(&clock,glue_frame_rate());
    double frequency=(double)SDL_GetPerformanceFrequency();
    double begin=SDL_GetPerformanceCounter()/frequency,last=begin,next_present=begin,report=begin;
    uint64_t ticks=0,presents=0,last_ticks=0,last_presents=0,last_dropped=0;
    int running=1,paused=0;
    while (running) {
        SDL_Event e; int reset_time=0;
        while (SDL_PollEvent(&e)) {
            if (e.type==SDL_QUIT) running=0;
            if (e.type==SDL_CONTROLLERDEVICEADDED || e.type==SDL_CONTROLLERDEVICEREMOVED) open_controller();
            int key=e.type==SDL_KEYDOWN && !e.key.repeat?e.key.keysym.sym:0;
            int cb=e.type==SDL_CONTROLLERBUTTONDOWN?e.cbutton.button:-1;
            int ours=controller && e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.which==SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
            if (!ours) cb=-1;
            if (key==SDLK_q && (e.key.keysym.mod & KMOD_CTRL)) running=0;
            if (key==SDLK_ESCAPE || cb==SDL_CONTROLLER_BUTTON_BACK) { paused=!paused; reset_time=1; }
            if (e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST && !headless) { paused=1; reset_time=1; }
            if (key==SDLK_F5 || cb==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                fprintf(stderr,"Quicksave: %s\n",glue_save_state_utf8(save)?"OK":"FAILED"); reset_time=1;
            }
            if (key==SDLK_F9 || cb==SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                fprintf(stderr,"Quickload: %s\n",glue_load_state_utf8(save)?"OK":"FAILED"); reset_time=1;
            }
            if (key==SDLK_F10) { wide=!wide; glue_set_widescreen(wide); }
            if (key==SDLK_F11) {
                fullscreen=!fullscreen;
                if (SDL_SetWindowFullscreen(window,fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0)) fprintf(stderr,"Fullscreen: %s\n",SDL_GetError());
                reset_time=1;
            }
        }
        double now=SDL_GetPerformanceCounter()/frequency;
        if (seconds>0 && now-begin>=seconds) break;
        if (reset_time) {
            clock.debt=0; last=now;
            if (audio) SDL_ClearQueuedAudio(audio);
            SDL_SetWindowTitle(window,paused?"Mushroom Recompiled - PAUSED | Esc/Back resume | F5 save | F9 load | Ctrl+Q quit":"Mushroom Recompiled");
        }
        unsigned due=paused?0:frame_clock_advance(&clock,now-last); last=now;
        glue_set_pad1(paused?0:read_pad());
        for (unsigned i=0;i<due;i++) {
            glue_step_frame(); ticks++;
            int16_t samples[4096]; size_t n;
            while ((n=glue_get_audio_samples(samples,4096,1))) {
                if (audio) {
                    if (SDL_GetQueuedAudioSize(audio)>48000*4/5) SDL_ClearQueuedAudio(audio);
                    if (SDL_QueueAudio(audio,samples,(Uint32)(n*sizeof(*samples)))) goto sdl_error;
                }
            }
        }
        if (now>=next_present) {
            int w,h,ow,oh;
            const uint32_t *pixels=glue_get_present_framebuffer(&w,&h);
            SDL_Rect src={0,0,w,h};
            if (SDL_UpdateTexture(texture,&src,pixels,w*4)) goto sdl_error;
            if (SDL_GetRendererOutputSize(renderer,&ow,&oh)) goto sdl_error;
            double aspect=wide?16.0/9:4.0/3;
            int dh=oh,dw=(int)lround(dh*aspect);
            if (dw>ow) { dw=ow; dh=(int)lround(dw/aspect); }
            SDL_Rect dst={(ow-dw)/2,(oh-dh)/2,dw,dh};
            SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer,texture,&src,&dst);
            if (paused) {
                SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer,0,0,0,160); SDL_RenderFillRect(renderer,NULL);
                SDL_SetRenderDrawColor(renderer,255,220,40,255);
                SDL_Rect bar={ow/2-24,oh/2-32,16,64}; SDL_RenderFillRect(renderer,&bar); bar.x+=32; SDL_RenderFillRect(renderer,&bar);
            }
            SDL_RenderPresent(renderer); presents++;
            next_present=now+(fps?1.0/fps:0);
            if (delay) SDL_Delay((Uint32)delay);
        }
        if (perf && now-report>=1) {
            fprintf(stderr,"perf elapsed=%.3f sim_hz=%.3f present_hz=%.3f ticks=%llu dropped=%llu dropped_total=%llu audio_bytes=%u paused=%d\n",
                now-begin,(ticks-last_ticks)/(now-report),(presents-last_presents)/(now-report),
                (unsigned long long)ticks,(unsigned long long)(clock.dropped-last_dropped),(unsigned long long)clock.dropped,
                audio?SDL_GetQueuedAudioSize(audio):0,paused);
            report=now; last_ticks=ticks; last_presents=presents; last_dropped=clock.dropped;
        }
        SDL_Delay(1);
    }
    result=0; goto done;
sdl_error:
    fprintf(stderr,"SDL: %s\n",SDL_GetError());
done:
    if (controller) SDL_GameControllerClose(controller);
    if (audio) SDL_CloseAudioDevice(audio);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    free(save); SDL_free(pref); SDL_Quit(); return result;
}
