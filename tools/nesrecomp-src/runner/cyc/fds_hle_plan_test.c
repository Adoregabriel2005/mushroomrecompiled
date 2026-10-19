/* fds_hle_plan_test.c - the FDS HLE tier's pure decisions (common/nes_fds_hle.h):
 * request parsing, source precedence, the whole plan matrix (every
 * combination of requests and capability facts), and the disk-ID matching
 * the auto swap uses. */
#include "../../common/nes_fds_hle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static NesFdsHleAsk ask(const char *s)
{
    NesFdsHleAsk a;
    CHECK(nes_fds_hle_parse(s, &a, NULL));
    return a;
}

static void parsing(void)
{
    NesFdsHleAsk a = ask(NULL);
    CHECK(a.auto_swap == -1 && a.fast_load == -1);
    a = ask("");
    CHECK(a.auto_swap == -1 && a.fast_load == -1);
    a = ask("auto-swap");
    CHECK(a.auto_swap == 1 && a.fast_load == -1);
    a = ask("fast-load");
    CHECK(a.auto_swap == -1 && a.fast_load == 1);
    a = ask("auto-swap,fast-load");
    CHECK(a.auto_swap == 1 && a.fast_load == 1);
    a = ask("  Auto_Swap + FAST-LOAD ");
    CHECK(a.auto_swap == 1 && a.fast_load == 1);
    a = ask("all");
    CHECK(a.auto_swap == 1 && a.fast_load == 1);
    a = ask("on");
    CHECK(a.auto_swap == 1 && a.fast_load == 1);
    a = ask("off");
    CHECK(a.auto_swap == 0 && a.fast_load == 0);
    a = ask("none");
    CHECK(a.auto_swap == 0 && a.fast_load == 0);
    a = ask("all,no-fast-load");
    CHECK(a.auto_swap == 1 && a.fast_load == 0);
    a = ask("all no-auto-swap");
    CHECK(a.auto_swap == 0 && a.fast_load == 1);
    a = ask("fast-load,off");            /* later words win */
    CHECK(a.auto_swap == 0 && a.fast_load == 0);
    const char *bad;
    CHECK(!nes_fds_hle_parse("auto-swap,turbo", &a, &bad) && bad && !strncmp(bad, "turbo", 5));
    CHECK(!nes_fds_hle_parse("autoswap", &a, &bad) && bad && !strcmp(bad, "autoswap"));
    CHECK(!nes_fds_hle_parse("fast-loads", &a, &bad));
    CHECK(!nes_fds_hle_parse("auto-swa", &a, &bad));
}

static NesFdsHleRequest capable(void)
{
    NesFdsHleRequest r;
    memset(&r, 0, sizeof(r));
    r.config = r.user = r.env = r.cli = r.live = NES_FDS_HLE_ASK_NONE;
    r.is_fds = true;
    r.have_anchor = true;
    r.sides = 2;
    r.sides_with_id = 2;
    return r;
}

static void precedence(void)
{
    static const int8_t V[3] = { -1, 0, 1 };
    static const char *const FROM[5] = { "game.toml", "settings", "env", "cli", "toggle" };
    /* Every combination of the five sources for both axes: the last source that
     * says something decides, and says where the decision came from. The two
     * axes read the sources in different orders so they are not in lockstep. */
    for (int c = 0; c < 3; ++c)
        for (int u = 0; u < 3; ++u)
            for (int e = 0; e < 3; ++e)
                for (int l = 0; l < 3; ++l)
                    for (int t = 0; t < 3; ++t) {
                        NesFdsHleRequest r = capable();
                        int8_t s[5] = { V[c], V[u], V[e], V[l], V[t] }, f[5] = { V[t], V[l], V[c], V[u], V[e] };
                        NesFdsHleAsk *src[5] = { &r.config, &r.user, &r.env, &r.cli, &r.live };
                        for (int i = 0; i < 5; ++i) { src[i]->auto_swap = s[i]; src[i]->fast_load = f[i]; }
                        NesFdsHlePlan p = nes_fds_hle_plan(r);
                        int8_t ws = 0, wf = 0;
                        const char *fs = "default", *ff = "default";
                        for (int i = 0; i < 5; ++i) {
                            if (s[i] >= 0) { ws = s[i]; fs = FROM[i]; }
                            if (f[i] >= 0) { wf = f[i]; ff = FROM[i]; }
                        }
                        CHECK(p.auto_swap == (ws == 1));
                        CHECK(p.fast_load == (wf == 1));
                        CHECK(!strcmp(p.auto_swap_from, fs));
                        CHECK(!strcmp(p.fast_load_from, ff));
                        CHECK(!p.auto_swap_denied && !p.fast_load_denied);
                        CHECK(p.observe);
                    }
    /* A zeroed source is an explicit "off", not silence: hosts must say NONE. */
    NesFdsHleRequest r = capable();
    r.config.auto_swap = 1;
    memset(&r.user, 0, sizeof(r.user));
    NesFdsHlePlan p = nes_fds_hle_plan(r);
    CHECK(!p.auto_swap && !strcmp(p.auto_swap_from, "settings"));
}

/* The axis table (nes_fds_hle_axes) names every axis the plan has, and its
 * accessors read exactly the plan's and the ask's fields. */
static void axes(void)
{
    unsigned n = 0;
    const NesFdsHleAxis *ax = nes_fds_hle_axes(&n);
    CHECK(n == 2);
    CHECK(!strcmp(ax[0].word, "auto-swap") && !strcmp(ax[1].word, "fast-load"));
    for (unsigned i = 0; i < n; ++i) {
        CHECK(ax[i].key && *ax[i].key && ax[i].label && *ax[i].label && ax[i].help && *ax[i].help);
        for (unsigned j = 0; j < i; ++j) CHECK(strcmp(ax[i].key, ax[j].key) && ax[i].ask != ax[j].ask);
        /* the axis's word asks for exactly its own field */
        NesFdsHleAsk a = ask(ax[i].word);
        for (unsigned j = 0; j < n; ++j)
            CHECK(*nes_fds_hle_ask_axis(&a, &ax[j]) == (i == j ? 1 : -1));
        char no[32];
        snprintf(no, sizeof(no), "no-%s", ax[i].word);
        a = ask(no);
        CHECK(*nes_fds_hle_ask_axis(&a, &ax[i]) == 0);
    }
    /* accessors against the named fields, on a granted and a refused plan */
    NesFdsHleRequest r = capable();
    r.cli.auto_swap = 1; r.live.fast_load = 1;
    NesFdsHlePlan p = nes_fds_hle_plan(r);
    CHECK(nes_fds_hle_plan_on(&p, &ax[0]) == p.auto_swap && nes_fds_hle_plan_on(&p, &ax[1]) == p.fast_load);
    CHECK(!strcmp(nes_fds_hle_plan_from(&p, &ax[0]), "cli") && !strcmp(nes_fds_hle_plan_from(&p, &ax[1]), "toggle"));
    r.have_anchor = false;
    p = nes_fds_hle_plan(r);
    CHECK(nes_fds_hle_plan_denied(&p, &ax[0]) && strstr(nes_fds_hle_plan_why(&p, &ax[0]), "anchor"));
    CHECK(!nes_fds_hle_plan_denied(&p, &ax[1]) && !nes_fds_hle_plan_why(&p, &ax[1]));
    /* writing through the ask accessor is the same as naming the field */
    NesFdsHleAsk w = NES_FDS_HLE_ASK_NONE;
    *nes_fds_hle_ask_axis(&w, &ax[1]) = 1;
    CHECK(w.fast_load == 1 && w.auto_swap == -1);
}

static void matrix(void)
{
    /* Every request x every capability fact. */
    for (int want_swap = 0; want_swap < 2; ++want_swap)
        for (int want_fast = 0; want_fast < 2; ++want_fast)
            for (int is_fds = 0; is_fds < 2; ++is_fds)
                for (int anchor = 0; anchor < 2; ++anchor)
                    for (unsigned sides = 0; sides <= 4; ++sides)
                        for (unsigned with_id = 0; with_id <= sides; ++with_id) {
                            NesFdsHleRequest r = capable();
                            r.cli.auto_swap = (int8_t)want_swap;
                            r.cli.fast_load = (int8_t)want_fast;
                            r.is_fds = is_fds;
                            r.have_anchor = anchor;
                            r.sides = sides;
                            r.sides_with_id = with_id;
                            NesFdsHlePlan p = nes_fds_hle_plan(r);
                            bool swap_ok = is_fds && anchor && sides >= 2 && with_id == sides;
                            CHECK(p.auto_swap == (want_swap && swap_ok));
                            CHECK(p.auto_swap_denied == (want_swap && !swap_ok));
                            CHECK((p.auto_swap_why != NULL) == p.auto_swap_denied);
                            /* fast load depends on its own request and the drive only */
                            CHECK(p.fast_load == (want_fast && is_fds));
                            CHECK(p.fast_load_denied == (want_fast && !is_fds));
                            CHECK((p.fast_load_why != NULL) == p.fast_load_denied);
                            CHECK(p.observe == (is_fds && anchor));
                            /* the axes are independent: flipping one request never
                             * changes the other axis's outcome */
                            NesFdsHleRequest q = r;
                            q.cli.auto_swap = (int8_t)!want_swap;
                            NesFdsHlePlan pq = nes_fds_hle_plan(q);
                            CHECK(pq.fast_load == p.fast_load && pq.fast_load_denied == p.fast_load_denied);
                            q = r;
                            q.cli.fast_load = (int8_t)!want_fast;
                            pq = nes_fds_hle_plan(q);
                            CHECK(pq.auto_swap == p.auto_swap && pq.auto_swap_denied == p.auto_swap_denied);
                            /* pure: the same request gives the same plan */
                            NesFdsHlePlan again = nes_fds_hle_plan(r);
                            CHECK(!memcmp(&again, &p, sizeof(p)));
                        }
    /* The reasons, one per missing capability, in order. */
    NesFdsHleRequest r = capable();
    r.cli.auto_swap = 1;
    r.is_fds = false;
    CHECK(strstr(nes_fds_hle_plan(r).auto_swap_why, "not a Famicom Disk System"));
    r = capable(); r.cli.auto_swap = 1; r.have_anchor = false;
    CHECK(strstr(nes_fds_hle_plan(r).auto_swap_why, "anchor"));
    r = capable(); r.cli.auto_swap = 1; r.sides = r.sides_with_id = 1;
    CHECK(strstr(nes_fds_hle_plan(r).auto_swap_why, "one side"));
    r = capable(); r.cli.auto_swap = 1; r.sides_with_id = 1;
    CHECK(strstr(nes_fds_hle_plan(r).auto_swap_why, "header"));
    /* Nothing asked: nothing on, nothing denied, whatever the capabilities. */
    r = capable();
    NesFdsHlePlan p = nes_fds_hle_plan(r);
    CHECK(!p.auto_swap && !p.fast_load && !p.auto_swap_denied && !p.fast_load_denied);
    CHECK(!strcmp(p.auto_swap_from, "default") && !strcmp(p.fast_load_from, "default"));
    /* A live toggle cannot force a refused axis. */
    r = capable(); r.have_anchor = false; r.live.auto_swap = 1;
    p = nes_fds_hle_plan(r);
    CHECK(!p.auto_swap && p.auto_swap_denied && !strcmp(p.auto_swap_from, "toggle"));
}

static void anchors(void)
{
    NesFdsHleAnchor a = nes_fds_hle_builtin_anchor(0x5E607DCFu);
    CHECK(a.id_check == 0xE445 && a.id_pointer == 0 && a.check_len == 3);
    CHECK(a.check[0] == 0x20 && a.check[1] == 0xE3 && a.check[2] == 0xE6);
    a = nes_fds_hle_builtin_anchor(0x12345678u);
    CHECK(a.id_check == 0);
}

static void matching(void)
{
    uint8_t block1[56] = { 0x01 };
    memcpy(block1 + 1, "*NINTENDO-HVC*", 14);
    const uint8_t hdr[10] = { 0xB1, 'O', 'T', 'O', 0x20, 0x00, 0x01, 0x00, 0x00, 0x0F };
    memcpy(block1 + 15, hdr, 10);
    uint8_t id[10];
    memcpy(id, hdr, 10);
    CHECK(nes_fds_hle_id_matches(id, block1));
    for (unsigned i = 0; i < 10; ++i) {
        memcpy(id, hdr, 10);
        id[i] ^= 1;
        CHECK(!nes_fds_hle_id_matches(id, block1));      /* every byte is compared */
        id[i] = 0xFF;
        CHECK(nes_fds_hle_id_matches(id, block1));       /* ...unless the ID says $FF */
    }
    memset(id, 0xFF, 10);
    CHECK(nes_fds_hle_id_matches(id, block1));
    /* block 1 out of a drive stream */
    uint8_t stream[200];
    memset(stream, 0, sizeof(stream));
    stream[100] = 0x80;
    memcpy(stream + 101, block1, 56);
    uint8_t out[56];
    CHECK(nes_fds_hle_block1(stream, sizeof(stream), out) && !memcmp(out, block1, 56));
    CHECK(!nes_fds_hle_block1(stream, 150, out));         /* cut short */
    stream[101] = 0x02;
    CHECK(!nes_fds_hle_block1(stream, sizeof(stream), out));   /* not block 1 */
    stream[101] = 0x01;
    stream[100] = 0x81;
    CHECK(!nes_fds_hle_block1(stream, sizeof(stream), out));   /* no gap mark */
    memset(stream, 0, sizeof(stream));
    CHECK(!nes_fds_hle_block1(stream, sizeof(stream), out));   /* blank */
}

int main(void)
{
    parsing();
    precedence();
    axes();
    matrix();
    anchors();
    matching();
    printf("fds_hle_plan_test: %u checks passed\n", checks);
    return 0;
}
