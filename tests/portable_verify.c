#include "portable_path.h"
#include "frame_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const double hz = 60.098813897;
    const int rates[] = {30,40,60};
    for (unsigned r=0;r<3;r++) {
        FrameClock c; frame_clock_init(&c,hz);
        unsigned ticks=0;
        for (int i=0;i<rates[r]*10;i++) ticks+=frame_clock_advance(&c,1.0/rates[r]);
        CHECK(ticks == 600); CHECK(c.dropped == 0);
    }
    FrameClock c; frame_clock_init(&c,hz);
    CHECK(frame_clock_advance(&c,2.0)==8); CHECK(c.dropped==112);
    CHECK(frame_clock_advance(&c,0)==0);
    CHECK(frame_clock_advance(&c,-1)==0);
    CHECK(frame_clock_advance(&c,1.0/hz)==1);
    setlocale(LC_ALL,"C");
    const wchar_t *path=L"portable-\u00e7-\u65e5-\U0001f344.tmp";
    FILE *f=mushroom_wfopen(path,L"wb"); CHECK(f); CHECK(fputs("portable",f)>=0); CHECK(!fclose(f));
    f=mushroom_wfopen(path,L"rb"); CHECK(f); CHECK(fgetc(f)=='p'); fclose(f);
    f=mushroom_fopen_utf8("portable-\xc3\xa7-\xe6\x97\xa5-\xf0\x9f\x8d\x84.tmp","rb"); CHECK(f); CHECK(fgetc(f)=='p'); fclose(f);
#ifdef _WIN32
    CHECK(!_wremove(path));
#else
    CHECK(!remove("portable-\xc3\xa7-\xe6\x97\xa5-\xf0\x9f\x8d\x84.tmp"));
    CHECK(!mushroom_wfopen(L"\xd800",L"rb"));
#endif
    puts("timing at 30/40/60 FPS, bounded stalls, Unicode paths: PASS"); return 0;
}
