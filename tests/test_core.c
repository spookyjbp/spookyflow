/* test_core.c - host unit tests for the portable core. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "anim.h"
#include "app.h"
#include "sfo.h"
#include "sound.h"
#include "imgscale.h"
#include "cover_cache.h"
#include "draw.h"
#include "font.h"
#include "icons.h"
#include "input.h"
#include "layout.h"
#include "library.h"
#include "log.h"
#include "menu.h"
#include "scanner.h"
#include "settings.h"
#include "title.h"
#include "ui.h"
#include "util.h"

static int g_fail, g_pass;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); if (strcmp(_a, _b) == 0) g_pass++; else { g_fail++; \
    printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b); } } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a = (a), _b = (b); if (fabs(_a - _b) <= (eps)) g_pass++; else { g_fail++; \
    printf("  FAIL %s:%d: %g !~ %g (eps %g)\n", __FILE__, __LINE__, _a, _b, (double)(eps)); } } while (0)

static Library g_lib;
static App g_app;

/* ------------------------------------------------------------------ */
static void clean_is(const char *raw, const char *want)
{
    char out[TITLE_MAX];
    title_clean(raw, out, sizeof(out));
    CHECK_STR(out, want);
}

static void test_title_clean(void)
{
    char id[TITLEID_MAX];
    printf("title cleaning\n");
    clean_is("Ape Escape - Pumped & Primed (USA)", "Ape Escape - Pumped & Primed");
    clean_is("Gran Turismo 4 (USA) (En,Fr,De,Es,It) [SCUS_973.28]", "Gran Turismo 4");
    clean_is("Gran Turismo 4 (USA) (En,Fr,De,Es,It) [SCUS_973.28].iso", "Gran Turismo 4");
    clean_is("Minecraft - PlayStation 3 Edition (USA).dec.iso", "Minecraft - PlayStation 3 Edition");
    clean_is("Terraria (USA) (En,Fr,De,Es,It).dec.iso", "Terraria");
    clean_is("Call of Duty - Black Ops II (USA).dec.iso.0", "Call of Duty - Black Ops II");
    clean_is("Okami (USA).BIN.ENC", "Okami");
    clean_is("Shadow Hearts - Covenant (USA) (Disc 1).iso", "Shadow Hearts - Covenant (Disc 1)");
    clean_is("Shadow Hearts - Covenant (USA) (Disc 2).iso", "Shadow Hearts - Covenant (Disc 2)");
    clean_is("Final Fantasy VII (USA) (Disc 3).cue", "Final Fantasy VII (Disc 3)");
    clean_is("Metal Gear Solid [Disc1of2] [SLUS-00594].bin", "Metal Gear Solid (Disc1of2)");
    clean_is("Xenogears (USA) (CD 2) [SLUS_006.69].cue", "Xenogears (CD 2)");
    clean_is("Discworld (USA).cue", "Discworld");
    clean_is("Game (Disco Edition).iso", "Game");
    clean_is("/dev_hdd0/PS2ISO/Okami (USA).iso", "Okami");
    clean_is("Final Fantasy VII (USA) (Disc 1).cue", "Final Fantasy VII (Disc 1)");
    clean_is("Metal_Gear_Solid_3.iso", "Metal Gear Solid 3");
    clean_is("Some Game_with_underscores and spaces.iso", "Some Game_with_underscores and spaces");
    clean_is("Big   Game   Title  -  (Europe).iso", "Big Game Title");
    clean_is("Game Name -.iso", "Game Name");
    clean_is("Killzone 2 [BCUS98116].iso.0", "Killzone 2");
    clean_is("Nested (a (b) c) Title [x].bin", "Nested Title");
    clean_is("[SLUS_209.14].iso", "[SLUS_209.14]");          /* all tags -> fallback, never empty */
    clean_is("Vib-Ribbon (Europe).cue", "Vib-Ribbon");
    clean_is("Mr. Driller (USA).cue", "Mr. Driller");

    CHECK(title_extract_id("Gran Turismo 4 (USA) [SCUS_973.28].iso", id, sizeof(id)) == 1);
    CHECK_STR(id, "SCUS97328");
    CHECK(title_extract_id("Demon's Souls [BLUS-30443].iso", id, sizeof(id)) == 1);
    CHECK_STR(id, "BLUS30443");
    CHECK(title_extract_id("BLES00932-Some Game", id, sizeof(id)) == 1);
    CHECK_STR(id, "BLES00932");
    CHECK(title_extract_id("Okami (USA).iso", id, sizeof(id)) == 0);
    CHECK_STR(id, "");
    CHECK(title_extract_id("Game [En,Fr].iso", id, sizeof(id)) == 0);
}

static void test_util(void)
{
    const char *s = "A\xC3\xA9\xE2\x80\xA6" "B\xFF";
    char buf[8];
    printf("util\n");
    CHECK(sf_utf8_next(&s) == 'A');
    CHECK(sf_utf8_next(&s) == 0xE9);
    CHECK(sf_utf8_next(&s) == 0x2026);
    CHECK(sf_utf8_next(&s) == 'B');
    CHECK(sf_utf8_next(&s) == 0xFF);           /* invalid byte -> Latin-1 */
    CHECK(sf_strlcpy(buf, "1234567890", sizeof(buf)) == 7);
    CHECK_STR(buf, "1234567");
    sf_format_size((uint64_t)2254857830ull, buf, sizeof(buf));
    CHECK_STR(buf, "2.1 GB");
    CHECK(sf_rgba(1, 2, 3, 4) == 0x01020304u);
    CHECK(sf_col_alpha(0xFFFFFFFFu, 0.5f) == 0xFFFFFF80u);
}

/* ------------------------------------------------------------------ */
static int idx_of(int plat, const char *title)
{
    const GameList *gl = &g_lib.tab[plat];
    int i;
    for (i = 0; i < gl->count; i++) if (strcmp(lib_title(&g_lib, &gl->games[i]), title) == 0) return i;
    return -1;
}

static void build_small_lib(void)
{
    static const char *names[] = {
        "Zelda-like.iso", "007 Agent.iso", "apple.iso", "Banana.iso", "The Matrix.iso",
        "Ape Escape.iso", "50 Cent.iso", ".hack.iso", "Dark Cloud.iso", "the getaway.iso", "Bully.iso"
    };
    int i;
    lib_clear(&g_lib);
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
        lib_add(&g_lib, PLAT_PS2, names[i], names[i], 1, NULL, 0, 0, 0);
}

static void test_sort(void)
{
    const GameList *gl = &g_lib.tab[PLAT_PS2];
    printf("sorting\n");
    build_small_lib();
    lib_sort(&g_lib, PLAT_PS2, 0);
    /* '#' bucket first (digits/symbols), then case-insensitive A..Z */
    CHECK_STR(lib_title(&g_lib, &gl->games[0]), ".hack");
    CHECK_STR(lib_title(&g_lib, &gl->games[1]), "007 Agent");
    CHECK_STR(lib_title(&g_lib, &gl->games[2]), "50 Cent");
    CHECK_STR(lib_title(&g_lib, &gl->games[3]), "Ape Escape");
    CHECK_STR(lib_title(&g_lib, &gl->games[4]), "apple");
    CHECK_STR(lib_title(&g_lib, &gl->games[5]), "Banana");
    CHECK(idx_of(PLAT_PS2, "The Matrix") > idx_of(PLAT_PS2, "the getaway"));   /* both under T */
    CHECK(gl->games[idx_of(PLAT_PS2, "The Matrix")].bucket == 20);
    CHECK(gl->first[0] == 0 && gl->first[1] == 3 && gl->first[3] == -1);
    /* ignore "The" */
    lib_sort(&g_lib, PLAT_PS2, 1);
    CHECK(gl->games[idx_of(PLAT_PS2, "The Matrix")].bucket == 13);
    CHECK(gl->games[idx_of(PLAT_PS2, "the getaway")].bucket == 7);
    CHECK(idx_of(PLAT_PS2, "the getaway") < idx_of(PLAT_PS2, "The Matrix"));
    CHECK(idx_of(PLAT_PS2, "Dark Cloud") < idx_of(PLAT_PS2, "the getaway"));
    CHECK(gl->first[20] == -1);
    CHECK(title_bucket("The", 1) == 20);       /* bare "The" is not stripped */
}

static void test_letters(void)
{
    const GameList *gl = &g_lib.tab[PLAT_PS2];
    int a0, b0, d0, z0, i;
    printf("letter jump\n");
    build_small_lib();
    lib_sort(&g_lib, PLAT_PS2, 0);
    a0 = gl->first[1]; b0 = gl->first[2]; d0 = gl->first[4]; z0 = gl->first[26];
    CHECK(lib_letter_next(gl, 0) == a0);                  /* # -> A */
    CHECK(lib_letter_next(gl, a0) == b0);                 /* A -> B */
    CHECK(lib_letter_next(gl, b0 + 1) == d0);             /* B (middle) -> D, C is empty */
    CHECK(lib_letter_next(gl, z0) == z0);                 /* Z is the end: no wrap */
    CHECK(lib_letter_prev(gl, b0 + 1) == b0);             /* middle of B -> start of B */
    CHECK(lib_letter_prev(gl, b0) == a0);                 /* start of B -> A */
    CHECK(lib_letter_prev(gl, d0) == b0);                 /* skips empty C */
    CHECK(lib_letter_prev(gl, 2) == 0);                   /* middle of # -> start */
    CHECK(lib_letter_prev(gl, 0) == 0);                   /* # start is the end: no wrap */
    CHECK(lib_letter_prev(gl, z0) < z0);                  /* Z still goes back */
    /* every jump lands on a bucket start */
    for (i = 0; i < gl->count; i++) {
        int n = lib_letter_next(gl, i), p = lib_letter_prev(gl, i);
        CHECK(gl->first[gl->games[n].bucket] == n);
        CHECK(gl->first[gl->games[p].bucket] == p);
    }
}

/* ------------------------------------------------------------------ */
static void test_spring(void)
{
    Spring s;
    float t = 0.0f, maxx = 0.0f, settle = -1.0f;
    printf("spring\n");
    spring_init(&s, 0.0f, SPRING_OMEGA_300MS);
    spring_set(&s, 1.0f);
    while (t < 2.0f) {
        spring_update(&s, 1.0f / 60.0f);
        t += 1.0f / 60.0f;
        if (s.x > maxx) maxx = s.x;
        if (settle < 0.0f && fabsf(s.x - 1.0f) < 0.01f) settle = t;
    }
    CHECK(maxx <= 1.0f + 1e-4f);                   /* critically damped: no overshoot */
    CHECK(settle > 0.15f && settle < 0.36f);       /* ~300 ms */
    CHECK(spring_settled(&s));
    /* variable / huge dt stays stable */
    spring_set(&s, -5.0f);
    spring_update(&s, 5.0f);
    CHECK(isfinite(s.x) && s.x >= -5.0f - 1e-3f && s.x <= 1.0f + 1e-3f);
    {
        float v = 0.0f;
        CHECK(anim_approach(&v, 1.0f, 2.0f, 0.25f) == 0 && fabsf(v - 0.5f) < 1e-6f);
        CHECK(anim_approach(&v, 1.0f, 2.0f, 0.5f) == 1 && v == 1.0f);
    }
}

/* ------------------------------------------------------------------ */
static Screen screen_ps3(int pw, int ph)
{
    Screen s;
    s.W = 848.0f; s.H = 512.0f;
    s.pa = ((float)pw / 848.0f) / ((float)ph / 512.0f);
    s.px = (float)ph / 512.0f;
    return s;
}

static Screen screen_direct(int pw, int ph)
{
    Screen s;
    s.W = (float)pw; s.H = (float)ph; s.pa = 1.0f; s.px = 1.0f;
    return s;
}

static void test_layout(void)
{
    Screen scr[4];
    int k, plat;
    printf("layout\n");
    scr[0] = screen_ps3(1280, 720); scr[1] = screen_ps3(1920, 1080);
    scr[2] = screen_direct(1280, 720); scr[3] = screen_direct(1920, 1080);
    for (k = 0; k < 4; k++) {
        d_set_screen(&scr[k]);
        for (plat = 0; plat < PLAT_COUNT; plat++) {
            CoverPos c0, c1, cm1, c2, ch;
            float sc = layout_case_scale(plat);
            layout_cover(&scr[k], plat, 0.0f, &c0);
            layout_cover(&scr[k], plat, 1.0f, &c1);
            layout_cover(&scr[k], plat, -1.0f, &cm1);
            layout_cover(&scr[k], plat, 2.0f, &c2);
            layout_cover(&scr[k], plat, 0.5f, &ch);
            CHECK_NEAR(c0.cx, scr[k].W * 0.5f, 1e-3);                 /* selected centered */
            CHECK_NEAR(c0.h, LY_SEL_H * sc * scr[k].H, 1e-3);          /* size fractions */
            CHECK_NEAR(c1.h, LY_NB_H * sc * scr[k].H, 1e-3);
            /* physical aspect preserved: w * pa / h == case aspect */
            CHECK_NEAR(c0.w * scr[k].pa / c0.h, layout_case_aspect(plat), 1e-4);
            CHECK_NEAR(c0.bright, 1.0f, 1e-6);
            CHECK(c1.bright < 1.0f && c2.bright < c1.bright);           /* dim with distance */
            CHECK_NEAR(c1.cx - c0.cx, c0.cx - cm1.cx, 1e-3);           /* symmetric */
            CHECK(c1.cx - c1.w * 0.5f > c0.cx + c0.w * 0.5f);          /* no overlap */
            CHECK(c2.cx - c2.w * 0.5f > c1.cx + c1.w * 0.5f);
            /* evenly spaced neighbors with gap proportional to width */
            CHECK_NEAR((c2.cx - c1.cx) / c1.w, 1.0f + LY_GAP_NB, 1e-4);
            CHECK(ch.cx > c0.cx && ch.cx < c1.cx && ch.h < c0.h && ch.h > c1.h);
            CHECK(c1.cy == c0.cy);
        }
        {
            CoverPos far;
            layout_cover(&scr[k], PLAT_PS2, 4.0f, &far);
            CHECK(far.cx + far.w * 0.5f > scr[k].W);   /* covers run off the edges */
        }
    }
}

/* Vertical stacking: tabs < title pill < case < chips < bar; the title is
 * centered between the tabs and the selected case, the chips between the
 * case and the bar, for every platform's case size. */
static void test_vertical_spacing(void)
{
    Screen scr[4];
    int k, plat;
    printf("vertical spacing\n");
    scr[0] = screen_ps3(1280, 720); scr[1] = screen_ps3(1920, 1080);
    scr[2] = screen_direct(1280, 720); scr[3] = screen_direct(1920, 1080);
    for (k = 0; k < 4; k++) {
        UiMetrics m;
        float H = scr[k].H, onepx = 1.0f / scr[k].px;
        float tabs_b, bar_t;
        d_set_screen(&scr[k]);
        layout_ui(&scr[k], &m);
        tabs_b = m.tabs_y + m.tab_h * 0.5f;
        bar_t = m.bar_y - m.bar_h * 0.5f;
        CHECK(m.tabs_y - m.tab_h * 0.5f > 0.0f);
        CHECK(m.bar_y + m.bar_h * 0.5f < H);
        for (plat = 0; plat < PLAT_COUNT; plat++) {
            CoverPos c;
            float ty, cy, case_t, case_b;
            layout_cover(&scr[k], plat, 0.0f, &c);
            layout_info_y(&scr[k], &m, plat, &ty, &cy);
            case_t = c.cy - c.h * 0.5f; case_b = c.cy + c.h * 0.5f;
            CHECK_NEAR(ty - tabs_b, case_t - ty, onepx);                  /* title centered */
            CHECK_NEAR(cy - case_b, bar_t - cy, onepx);                   /* chips centered */
            CHECK(tabs_b + 0.01f * H < ty - m.title_h * 0.5f);            /* nothing touches */
            CHECK(ty + m.title_h * 0.5f + 0.01f * H < case_t);
            CHECK(case_b + 0.01f * H < cy - m.chip_h * 0.5f);
            CHECK(cy + m.chip_h * 0.5f + 0.01f * H < bar_t);
        }
        CHECK(fabsf(m.title_y - (tabs_b + (LY_FLOW_CY - LY_SEL_H * 0.5f) * H) * 0.5f) < onepx);   /* default = full-size case */
    }
    /* the cover window matches real cover art shapes */
    CHECK_NEAR(layout_window_aspect(PLAT_PS1), 1.00, 0.005);
    CHECK_NEAR(layout_window_aspect(PLAT_PS2), 0.70, 0.005);
    CHECK_NEAR(layout_window_aspect(PLAT_PS3), 0.86, 0.005);
}

static void test_bar_layout(void)
{
    Screen scr[4];
    int k, mode, i;
    printf("bar layout\n");
    scr[0] = screen_ps3(1280, 720); scr[1] = screen_ps3(1920, 1080);
    scr[2] = screen_direct(1280, 720); scr[3] = screen_direct(1920, 1080);
    for (k = 0; k < 4; k++) {
        d_set_screen(&scr[k]);
        for (mode = 0; mode < 2; mode++) {
            BarLayout L;
            float px_x = scr[k].px * scr[k].pa;   /* physical px per logical x unit */
            float gaps[BAR_MAX_GROUPS + 1];
            ui_bar_layout(mode, &L);
            CHECK(L.n == (mode ? 2 : 5));
            CHECK_NEAR(L.bar_x, scr[k].W * 0.015f, 1e-3);
            CHECK_NEAR(L.bar_x + L.bar_w, scr[k].W * 0.985f, 1e-3);
            gaps[0] = L.g[0].x - L.bar_x;
            for (i = 1; i < L.n; i++) gaps[i] = L.g[i].x - (L.g[i - 1].x + L.g[i - 1].w);
            gaps[L.n] = (L.bar_x + L.bar_w) - (L.g[L.n - 1].x + L.g[L.n - 1].w);
            for (i = 0; i <= L.n; i++) {
                CHECK(gaps[i] > 0.0f);                                   /* nothing overlaps, fits */
                CHECK(fabsf(gaps[i] - gaps[0]) * px_x <= 1.0f);          /* equal within 1 px */
            }
            CHECK(gaps[0] * px_x > 20.0f);                               /* visibly spread */
        }
    }
    {
        float w[3] = { 10, 20, 30 }, x[3];
        float g = layout_space_evenly(0, 100, w, 3, x);
        CHECK_NEAR(g, 10.0, 1e-5);
        CHECK_NEAR(x[0], 10.0, 1e-5); CHECK_NEAR(x[1], 30.0, 1e-5); CHECK_NEAR(x[2], 60.0, 1e-5);
    }
}

static void test_title_fit(void)
{
    Screen s = screen_ps3(1280, 720);
    float em, w;
    int trunc;
    printf("title pill fit\n");
    d_set_screen(&s);
    w = ui_title_fit("Okami", 500.0f, 18.0f, &em, &trunc);
    CHECK(trunc == 0 && em == 18.0f && w > 0.0f && w < 500.0f);
    w = ui_title_fit("A fairly long title that needs shrinking", d_text_width(&g_font_main, 18.0f, "A fairly long title that needs shrinking") * 0.95f, 18.0f, &em, &trunc);
    CHECK(trunc == 0 && em < 18.0f);
    w = ui_title_fit("An extremely long title that cannot possibly fit in the pill at all, ever", 120.0f, 18.0f, &em, &trunc);
    CHECK(trunc == 1 && w == 120.0f);
}

/* ------------------------------------------------------------------ */
static void test_settings(void)
{
    Settings a, b;
    GameCfg g, r;
    char val[48];
    const char *path = "build-host/test_settings.ini";
    printf("settings\n");
    settings_defaults(&a);
    CHECK(a.autoplay == 0 && a.music_volume == 50 && a.shuffle == 1 && a.background == BG_ANIMATED && a.ignore_the == 0);
    a.music_volume = 35; a.ignore_the = 1; a.last_tab = 2;
    sf_strlcpy(a.last_key[1], "SCUS97328", SET_KEY_MAX);
    memset(&g, 0, sizeof(g));
    sf_strlcpy(g.key, "Some Game = weird (USA).iso", sizeof(g.key));
    g.autoplay = AP_ON; g.rsx = 3; g.offline = 1;
    CHECK(settings_game_set(&a, &g) == 0);
    sf_strlcpy(g.key, "SLUS20914", sizeof(g.key));
    g.autoplay = AP_OFF; g.rsx = 0; g.offline = 0; g.emulator = 1;
    CHECK(settings_game_set(&a, &g) == 0);
    CHECK(settings_save(&a, path) == 0);
    settings_defaults(&b);
    CHECK(settings_load(&b, path) == 0);
    CHECK(b.music_volume == 35 && b.ignore_the == 1 && b.last_tab == 2);
    CHECK_STR(b.last_key[1], "SCUS97328");
    CHECK(b.game_count == 2);
    r = settings_game_get(&b, "Some Game = weird (USA).iso");
    CHECK(r.autoplay == AP_ON && r.rsx == 3 && r.offline == 1);
    /* offline mode: per-game Default follows the global setting */
    {
        Settings so;
        GameCfg go;
        char txt[48];
        settings_defaults(&so);
        memset(&go, 0, sizeof(go));
        CHECK(so.offline == 0 && settings_offline_effective(&so, &go) == 0);
        so.offline = 1;
        CHECK(settings_offline_effective(&so, &go) == 1);
        menu_value_text(ROW_G_OFFLINE, PLAT_PS3, &so, &go, txt, sizeof(txt));
        CHECK_STR(txt, "Default (On)");
        go.offline = AP_OFF;
        CHECK(settings_offline_effective(&so, &go) == 0);
        go.offline = AP_ON; so.offline = 0;
        CHECK(settings_offline_effective(&so, &go) == 1);
        CHECK(menu_change(ROW_F_OFFLINE, 0, 0, &so, &go) == MENU_FX_CHANGED && so.offline == 1);
        CHECK(menu_change(ROW_G_OFFLINE, PLAT_PS2, +1, &so, &go) == MENU_FX_CHANGED && go.offline == AP_OFF);
        settings_parse_line(&so, "ui_color=0");
        CHECK(so.ui_color == UI_COLOR_BLUE);
        menu_value_text(ROW_F_UI_COLOR, 0, &so, &go, txt, sizeof(txt));
        CHECK_STR(txt, "Blue");
        CHECK(menu_change(ROW_F_UI_COLOR, 0, -1, &so, &go) == MENU_FX_CHANGED && so.ui_color == UI_COLOR_WHITE);
        settings_parse_line(&so, "ui_color=99");
        CHECK(so.ui_color == UI_COLOR_WHITE);                      /* out of range: clamped */
        settings_defaults(&so);
        CHECK(so.sfx_volume == 50 && so.music_volume == 50 && so.ui_color == UI_COLOR_BLUE);
        CHECK(so.cases == 1 && so.sleep_min == 5);
        menu_value_text(ROW_F_SFX, 0, &so, &go, txt, sizeof(txt));
        CHECK_STR(txt, "50%");
        CHECK(menu_change(ROW_F_SFX, 0, -1, &so, &go) == MENU_FX_CHANGED && so.sfx_volume == 45);
        settings_parse_line(&so, "sound_effects=0");            /* fix 13 file: Off */
        CHECK(so.sfx_volume == 0);
        menu_value_text(ROW_F_SFX, 0, &so, &go, txt, sizeof(txt));
        CHECK_STR(txt, "Off");
        CHECK(menu_is_slider(ROW_F_SFX));
        settings_parse_line(&so, "offline=1");
        CHECK(so.offline == 1);
        settings_parse_line(&so, "game 0 0 2 0 Old Game.iso");
        CHECK(settings_game_get(&so, "Old Game.iso").offline == AP_OFF);
    }
    r = settings_game_get(&b, "SLUS20914");
    CHECK(r.autoplay == AP_OFF && r.emulator == 1);
    /* resetting to defaults removes the entry */
    memset(&r, 0, sizeof(r));
    sf_strlcpy(r.key, "SLUS20914", sizeof(r.key));
    CHECK(settings_game_set(&b, &r) == 0 && b.game_count == 1);
    /* malformed lines are ignored, values clamped */
    settings_parse_line(&b, "music_volume=900");
    CHECK(b.music_volume == 100);
    settings_parse_line(&b, "garbage line");
    settings_parse_line(&b, "game x y z");
    CHECK(b.game_count == 1);
    CHECK(settings_load(&b, "build-host/does_not_exist.ini") == -1);

    /* per-game auto-play resolution */
    settings_defaults(&a);
    memset(&g, 0, sizeof(g));
    sf_strlcpy(g.key, "X", sizeof(g.key));
    CHECK(settings_autoplay_effective(&a, &g) == 0);
    menu_value_text(ROW_G_AUTOPLAY, PLAT_PS2, &a, &g, val, sizeof(val));
    CHECK_STR(val, "Default (Off)");
    a.autoplay = 1;
    CHECK(settings_autoplay_effective(&a, &g) == 1);
    menu_value_text(ROW_G_AUTOPLAY, PLAT_PS2, &a, &g, val, sizeof(val));
    CHECK_STR(val, "Default (On)");
    g.autoplay = AP_OFF;
    CHECK(settings_autoplay_effective(&a, &g) == 0);
    g.autoplay = AP_ON; a.autoplay = 0;
    CHECK(settings_autoplay_effective(&a, &g) == 1);
}

static void test_menu(void)
{
    RowKind r[MENU_MAX_ROWS];
    Settings s;
    GameCfg g;
    char v[48];
    int i;
    printf("menus\n");
    CHECK(menu_rows(PANEL_GAME, PLAT_PS1, r, MENU_MAX_ROWS) == 2);
    CHECK(r[0] == ROW_G_AUTOPLAY && r[1] == ROW_G_EMULATOR);
    CHECK(menu_rows(PANEL_GAME, PLAT_PS2, r, MENU_MAX_ROWS) == 5);
    CHECK(r[4] == ROW_G_PS2CFG);
    CHECK(menu_rows(PANEL_GAME, PLAT_PS3, r, MENU_MAX_ROWS) == 3);
    CHECK(menu_rows(PANEL_FLOW, 0, r, MENU_MAX_ROWS) == 16);
    CHECK(r[1] == ROW_F_OFFLINE && r[2] == ROW_F_SFX && r[6] == ROW_F_UI_COLOR && r[7] == ROW_F_INFO && r[8] == ROW_F_CASES && r[9] == ROW_F_SLEEP &&
          r[12] == ROW_F_XMB_TOP && r[14] == ROW_F_QUIT && r[15] == ROW_F_POWEROFF);
    settings_defaults(&s);
    memset(&g, 0, sizeof(g));
    menu_value_text(ROW_G_EMULATOR, PLAT_PS1, &s, &g, v, sizeof(v)); CHECK_STR(v, "Standard");
    menu_value_text(ROW_G_EMULATOR, PLAT_PS2, &s, &g, v, sizeof(v)); CHECK_STR(v, "Default");
    CHECK(menu_change(ROW_G_AUTOPLAY, PLAT_PS2, -1, &s, &g) == MENU_FX_CHANGED && g.autoplay == AP_OFF);
    CHECK(menu_change(ROW_G_AUTOPLAY, PLAT_PS2, 0, &s, &g) == MENU_FX_CHANGED && g.autoplay == AP_DEFAULT);
    for (i = 0; i < 30; i++) menu_change(ROW_F_VOLUME, 0, +1, &s, &g);
    CHECK(s.music_volume == 100);
    CHECK(menu_change(ROW_F_VOLUME, 0, +1, &s, &g) == MENU_FX_NONE);
    CHECK(menu_change(ROW_F_IGNORE_THE, 0, 1, &s, &g) == MENU_FX_RESORT && s.ignore_the == 1);
    CHECK(menu_change(ROW_F_QUIT, 0, 0, &s, &g) == MENU_FX_QUIT);
    CHECK(menu_change(ROW_F_QUIT, 0, 1, &s, &g) == MENU_FX_NONE);   /* left/right never quits */
}

/* ------------------------------------------------------------------ */
static void test_input_repeat(void)
{
    Input in;
    float t = 0.0f, dt = 1.0f / 60.0f, first = -1.0f, last = 0.0f, early_iv = 0.0f, late_iv = 0.0f;
    int fires = 0;
    printf("input repeat\n");
    input_reset(&in);
    input_update(&in, SFB_RIGHT, dt);
    CHECK((in.fired & SFB_RIGHT) && (in.pressed & SFB_RIGHT));
    while (t < 2.5f) {
        input_update(&in, SFB_RIGHT, dt);
        t += dt;
        if (in.fired & SFB_RIGHT) {
            fires++;
            if (first < 0.0f) first = t;
            if (fires == 2) early_iv = t - last;
            late_iv = t - last;
            last = t;
        }
    }
    CHECK(first >= REPEAT_DELAY - dt && first <= REPEAT_DELAY + 2 * dt);
    CHECK(late_iv < early_iv);                     /* accelerates */
    CHECK(late_iv <= REPEAT_FAST + dt + 1e-4f);
    input_update(&in, 0, dt);
    CHECK(in.released & SFB_RIGHT);
    input_update(&in, SFB_CROSS, dt);
    input_update(&in, SFB_CROSS, 1.0f);
    CHECK(!(in.fired & SFB_CROSS));                /* face buttons don't repeat */
}

static void test_hold_exit(void)
{
    HoldExit h;
    float dt = 1.0f / 60.0f, t;
    int done = 0, i;
    printf("hold-to-exit\n");
    /* tap: nothing */
    hold_exit_reset(&h);
    hold_exit_update(&h, 1, 1, 1, dt);
    for (i = 0; i < 6; i++) done |= hold_exit_update(&h, 0, 1, 1, dt);
    done |= hold_exit_update(&h, 0, 0, 1, dt);
    CHECK(!done && !h.armed && h.t == 0.0f);
    /* full hold: completes only after HOLD_EXIT_TIME */
    hold_exit_reset(&h);
    done = hold_exit_update(&h, 1, 1, 1, dt);
    t = dt;
    while (!done && t < 3.0f) { done = hold_exit_update(&h, 0, 1, 1, dt); t += dt; }
    CHECK(done && t >= HOLD_EXIT_TIME - 1e-3f && t <= HOLD_EXIT_TIME + 2 * dt);
    /* release at 1.4 s resets; ring unwinds smoothly */
    hold_exit_reset(&h);
    hold_exit_update(&h, 1, 1, 1, dt);
    for (t = dt; t < 1.4f; t += dt) CHECK(!hold_exit_update(&h, 0, 1, 1, dt));
    CHECK(h.shown > 0.85f);
    hold_exit_update(&h, 0, 0, 1, dt);
    CHECK(h.t == 0.0f && h.shown > 0.8f && h.shown < 0.95f);    /* animating back, not snapped */
    for (i = 0; i < 60; i++) hold_exit_update(&h, 0, 0, 1, dt);
    CHECK(h.shown == 0.0f);
    /* still holding after release-press cycle restarts from zero */
    hold_exit_update(&h, 1, 1, 1, dt);
    for (t = dt; t < 1.0f; t += dt) CHECK(!hold_exit_update(&h, 0, 1, 1, dt));
    /* press while a panel is open (not main context) never arms, even if held after */
    hold_exit_reset(&h);
    hold_exit_update(&h, 1, 1, 0, dt);
    done = 0;
    for (t = 0; t < 3.0f; t += dt) done |= hold_exit_update(&h, 0, 1, 1, dt);
    CHECK(!done && h.shown == 0.0f);
    /* main context lost mid-hold cancels */
    hold_exit_reset(&h);
    hold_exit_update(&h, 1, 1, 1, dt);
    for (t = 0; t < 1.0f; t += dt) hold_exit_update(&h, 0, 1, 1, dt);
    done = hold_exit_update(&h, 0, 1, 0, dt);
    for (t = 0; t < 1.0f; t += dt) done |= hold_exit_update(&h, 0, 1, 1, dt);
    CHECK(!done);
}

/* ------------------------------------------------------------------ */
static void test_cover_cache(void)
{
    CoverCache cc;
    int f, i, center = 100;
    printf("cover cache\n");
    cc_init(&cc, CC_DEFAULT_BUDGET, NULL, NULL);
    for (f = 0; f < 40; f++) cc_update(&cc, 0, center, 500);
    CHECK(cc_resident_count(&cc) <= CC_DEFAULT_BUDGET);
    CHECK(cc_resident_count(&cc) >= CC_MAIN_WINDOW - 1);   /* one tab: its window */
    for (i = center - 7; i <= center + 7; i++) CHECK(cc_lookup(&cc, 0, i) >= 0);   /* drawn covers resident */
    /* moving far evicts old ones and never exceeds budget */
    for (center = 100; center < 300; center += 3)
        for (f = 0; f < 10; f++) { cc_update(&cc, 0, center, 500); CHECK(cc_resident_count(&cc) <= CC_DEFAULT_BUDGET); }
    CHECK(cc_lookup(&cc, 0, 100) < 0);
    for (i = 297 - 7; i <= 297 + 7; i++) CHECK(cc_lookup(&cc, 0, i) >= 0);
    /* switching tab replaces other-tab slots */
    for (f = 0; f < 40; f++) cc_update(&cc, 1, 5, 20);
    CHECK(cc_lookup(&cc, 1, 0) >= 0 && cc_lookup(&cc, 1, 19) >= 0);
    CHECK(cc_resident_count(&cc) <= CC_DEFAULT_BUDGET);
    /* loads are rate limited */
    cc_init(&cc, CC_DEFAULT_BUDGET, NULL, NULL);
    cc_update(&cc, 0, 50, 500);
    CHECK(cc_resident_count(&cc) == CC_LOADS_PER_FRAME);
}

/* ------------------------------------------------------------------ */
/* Fake filesystem for the scanner */
typedef struct { const char *dir; const char *name; int is_dir; uint64_t size; } FakeEnt;
static const FakeEnt k_fake[] = {
    { "/dev_hdd0/PSXISO", "Crash Bandicoot (USA).cue", 0, 1000 },
    { "/dev_hdd0/PSXISO", "Crash Bandicoot (USA).bin", 0, 400000000 },
    { "/dev_hdd0/PSXISO", "Tekken 3 (USA).bin", 0, 450000000 },           /* bin without cue */
    { "/dev_hdd0/PSXISO", "Final Fantasy VII", 1, 0 },
    { "/dev_hdd0/PSXISO/Final Fantasy VII", "Final Fantasy VII (USA) (Disc 1).cue", 0, 900 },
    { "/dev_hdd0/PSXISO/Final Fantasy VII", "Final Fantasy VII (USA) (Disc 1).bin", 0, 700000000 },
    { "/dev_hdd0/PSXISO/Final Fantasy VII", "deeper", 1, 0 },
    { "/dev_hdd0/PSXISO/Final Fantasy VII/deeper", "Ignored (USA).cue", 0, 1 },
    { "/dev_hdd0/PS2ISO", "Okami (USA).iso", 0, 4000000000ull },
    { "/dev_hdd0/PS2ISO", "God of War II (USA).iso.0", 0, 4294967295ull },
    { "/dev_hdd0/PS2ISO", "God of War II (USA).iso.1", 0, 3000000000ull },
    { "/dev_hdd0/PS2ISO", "readme.txt", 0, 10 },
    { "/dev_hdd0/PS2ISO", ".hidden.iso", 0, 10 },
    { "/dev_hdd0/PS3ISO", "Demon's Souls [BLUS30443].iso", 0, 6000000000ull },
    { "/dev_hdd0/GAMES", "BLES00932-Some Game", 1, 0 },
    { "/dev_hdd0/GAMES", "Folder Game", 1, 0 },
    { "/dev_hdd0/GAMES", "loose.iso", 0, 10 },
};

static int fake_list(void *ctx, const char *dir, FsEntry *out, int max)
{
    int i, n = 0, found = 0;
    (void)ctx;
    for (i = 0; i < (int)(sizeof(k_fake) / sizeof(k_fake[0])); i++) {
        if (strcmp(k_fake[i].dir, dir) != 0) continue;
        found = 1;
        if (n >= max) break;
        sf_strlcpy(out[n].name, k_fake[i].name, FS_NAME_MAX);
        out[n].is_dir = k_fake[i].is_dir;
        out[n].size = k_fake[i].size;
        n++;
    }
    /* directories that exist but are empty */
    if (!found && strcmp(dir, "/dev_hdd0/PSXISO/Final Fantasy VII/deeper") != 0) return -1;
    return n;
}

static int fake_none(void *ctx, const char *dir, FsEntry *out, int max)
{
    (void)ctx; (void)dir; (void)out; (void)max;
    return -1;
}

static void test_scanner(void)
{
    Scanner sc;
    int n, steps = 0, i;
    const ScanRoot *roots = scanner_default_roots(&n);
    const GameList *ps1 = &g_lib.tab[PLAT_PS1], *ps2 = &g_lib.tab[PLAT_PS2], *ps3 = &g_lib.tab[PLAT_PS3];
    printf("scanner\n");
    lib_clear(&g_lib);
    scanner_begin(&sc, roots, n);
    while (!scanner_step(&sc, &g_lib, fake_list, NULL) && steps < 1000) {
        steps++;
        CHECK(scanner_progress(&sc) >= 0.0f && scanner_progress(&sc) <= 1.0f);
    }
    CHECK(sc.done && steps < 1000);
    for (i = 0; i < PLAT_COUNT; i++) lib_sort(&g_lib, i, 0);
    CHECK(ps1->count == 3);                                  /* Crash (cue), Tekken (bin), FF7 (subfolder) */
    i = idx_of(PLAT_PS1, "Crash Bandicoot");
    CHECK(i >= 0 && ps1->games[i].size == 400001000ull);   /* bin size attached to cue */
    CHECK(idx_of(PLAT_PS1, "Tekken 3") >= 0);
    CHECK(idx_of(PLAT_PS1, "Final Fantasy VII (Disc 1)") >= 0);
    CHECK(idx_of(PLAT_PS1, "Ignored") < 0);                  /* only one level deep */
    CHECK(ps2->count == 2);
    i = idx_of(PLAT_PS2, "God of War II");
    CHECK(i >= 0 && ps2->games[i].size == 7294967295ull);  /* split parts summed */
    CHECK(ps3->count == 3);
    i = idx_of(PLAT_PS3, "Demon's Souls");
    CHECK(i >= 0);
    CHECK_STR(lib_id(&g_lib, &ps3->games[i]), "BLUS30443");
    CHECK_STR(lib_key(&g_lib, &ps3->games[i]), "BLUS30443");
    i = idx_of(PLAT_PS3, "Folder Game");
    CHECK(i >= 0 && ps3->games[i].size == 0);
    CHECK_STR(lib_key(&g_lib, &ps3->games[i]), "Folder Game");
    CHECK_STR(lib_path(&g_lib, &ps3->games[i]), "/dev_hdd0/GAMES/Folder Game");
    CHECK(sc.found[0] == 3 && sc.found[1] == 2 && sc.found[2] == 3);
}

/* real background thread, like the PS3's sysThreadCreate path */
#include <pthread.h>
typedef struct { void (*fn)(void *); void *arg; } TestJob;
static TestJob g_tjob;
static pthread_t g_tthread;
static int g_tthread_started;
static void *tjob_entry(void *p) { TestJob *j = (TestJob *)p; j->fn(j->arg); return NULL; }
static int test_run_async(void (*fn)(void *), void *arg)
{
    g_tjob.fn = fn; g_tjob.arg = arg;
    if (pthread_create(&g_tthread, NULL, tjob_entry, &g_tjob) != 0) return -1;
    g_tthread_started = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
static void test_imgscale(void)
{
    static uint8_t src[400 * 100 * 4], dst[100 * 25 * 4];
    int ow, oh, x, y, lit = 0, dark_fringe = 0;
    printf("image scaling\n");
    img_fit_size(400, 100, 100.0f, 60.0f, &ow, &oh);
    CHECK(ow == 100 && oh == 25);                          /* width-limited */
    img_fit_size(100, 100, 300.0f, 40.0f, &ow, &oh);
    CHECK(ow == 40 && oh == 40);                           /* height-limited */
    img_fit_size(50, 20, 300.0f, 300.0f, &ow, &oh);
    CHECK(ow == 50 && oh == 20);                           /* never enlarges */
    /* white 1-pixel diagonal lines on transparent: after a 4x shrink every
     * output row along the line keeps some coverage (no skipped pixels) and
     * edge pixels stay white, not grey */
    memset(src, 0, sizeof(src));
    for (y = 0; y < 100; y++)
        for (x = 0; x < 400; x++)
            if ((x + y) % 40 == 0) { uint8_t *p = src + (y * 400 + x) * 4; p[0] = p[1] = p[2] = 255; p[3] = 255; }
    img_resample_rgba(src, 400, 100, dst, 100, 25);
    for (y = 0; y < 25; y++) {
        int row_lit = 0;
        for (x = 0; x < 100; x++) {
            const uint8_t *d = dst + (y * 100 + x) * 4;
            if (d[3] > 0) { row_lit++; lit++; if (d[0] < 250) dark_fringe++; }
        }
        CHECK(row_lit >= 9);
    }
    CHECK(lit > 0 && dark_fringe == 0);
    /* margin: transparent border, edge color repeated, inside untouched */
    {
        static uint8_t in[3 * 2 * 4], out[7 * 6 * 4];
        int i;
        for (i = 0; i < 6; i++) { in[i * 4] = (uint8_t)(10 * i); in[i * 4 + 1] = 200; in[i * 4 + 2] = 30; in[i * 4 + 3] = 255; }
        img_pad_rgba(in, 3, 2, 2, out);
        CHECK(out[3] == 0 && out[(5 * 7 + 6) * 4 + 3] == 0);              /* corners transparent */
        CHECK(out[(2 * 7 + 2) * 4 + 3] == 255 && out[(2 * 7 + 2) * 4] == 0); /* first pixel */
        CHECK(out[(3 * 7 + 4) * 4] == 50 && out[(3 * 7 + 4) * 4 + 3] == 255); /* last pixel */
        CHECK(out[(2 * 7 + 0) * 4] == 0 && out[(2 * 7 + 0) * 4 + 1] == 200);  /* left margin repeats color */
    }
    /* fully transparent pixels next to the line carry its color */
    {
        int bled = 0;
        for (x = 0; x < 100 * 25; x++) if (dst[x * 4 + 3] == 0 && dst[x * 4] > 200) bled++;
        CHECK(bled > 0);
    }
}

static int g_power_off_calls;
static int power_off_ok(void) { g_power_off_calls++; return 0; }
static void run_frames(uint32_t held, float secs);
static void tap(uint32_t b);

/* 16-bit stereo WAV in memory */
static size_t make_wav(unsigned char *b, int rate, int frames, int16_t l, int16_t r)
{
    size_t data = (size_t)frames * 4, i;
    memcpy(b, "RIFF", 4); b[4] = (unsigned char)(36 + data); b[5] = (unsigned char)((36 + data) >> 8); b[6] = b[7] = 0;
    memcpy(b + 8, "WAVEfmt ", 8);
    b[16] = 16; b[17] = b[18] = b[19] = 0;
    b[20] = 1; b[21] = 0; b[22] = 2; b[23] = 0;
    b[24] = (unsigned char)rate; b[25] = (unsigned char)(rate >> 8); b[26] = (unsigned char)(rate >> 16); b[27] = 0;
    b[28] = b[29] = b[30] = b[31] = 0;
    b[32] = 4; b[33] = 0; b[34] = 16; b[35] = 0;
    memcpy(b + 36, "data", 4);
    b[40] = (unsigned char)data; b[41] = (unsigned char)(data >> 8); b[42] = b[43] = 0;
    for (i = 0; i < (size_t)frames; i++) {
        b[44 + i * 4] = (unsigned char)l; b[45 + i * 4] = (unsigned char)((uint16_t)l >> 8);
        b[46 + i * 4] = (unsigned char)r; b[47 + i * 4] = (unsigned char)((uint16_t)r >> 8);
    }
    return 44 + data;
}

static void test_sound(void)
{
    static unsigned char wav[44 + 4 * 2400];
    static float out[2 * 1024];
    size_t n;
    int i, id, nonzero;
    float peak;
    printf("sound\n");
    sound_init();
    for (id = 0; id < SND_COUNT; id++) {
        CHECK(sound_length((SoundId)id) > 0 && !sound_is_custom((SoundId)id));
        CHECK(strstr(sound_file_name((SoundId)id), ".wav") != NULL);
    }
    /* silence when nothing plays */
    sound_set_enabled(1); sound_set_volume(1.0f);
    sound_mix(out, 1024);
    for (i = 0, nonzero = 0; i < 2048; i++) if (out[i] != 0.0f) nonzero++;
    CHECK(nonzero == 0);
    /* a played sound comes out on both channels, within -1..1 */
    sound_play(SND_CONFIRM);
    sound_mix(out, 1024);
    for (i = 0, peak = 0.0f; i < 1024; i++) {
        CHECK(out[i * 2] == out[i * 2 + 1]);
        if (fabsf(out[i * 2]) > peak) peak = fabsf(out[i * 2]);
    }
    CHECK(peak > 0.05f && peak <= 1.0f);
    for (i = 0; i < 40; i++) sound_mix(out, 1024);       /* let it finish */
    /* many rapid plays never overflow or clip past 1 */
    for (i = 0; i < 100; i++) sound_play(SND_MOVE);
    for (i = 0; i < 10; i++) { int k; sound_mix(out, 1024); for (k = 0; k < 2048; k++) CHECK(out[k] <= 1.0f && out[k] >= -1.0f); }
    for (i = 0; i < 40; i++) sound_mix(out, 1024);
    /* disabled: silent, but plays are still counted (the app still asks) */
    sound_set_enabled(0);
    { uint32_t c = sound_play_count(SND_TAB); sound_play(SND_TAB); CHECK(sound_play_count(SND_TAB) == c + 1); }
    sound_mix(out, 1024);
    for (i = 0, nonzero = 0; i < 2048; i++) if (out[i] != 0.0f) nonzero++;
    CHECK(nonzero == 0);
    sound_set_enabled(1);
    /* user WAV: 24 kHz stereo -> 48 kHz mono, twice as long */
    n = make_wav(wav, 24000, 2400, 16384, 16384);
    CHECK(sound_load_wav_mem(SND_MOVE, wav, n) == 0);
    CHECK(sound_is_custom(SND_MOVE) && sound_length(SND_MOVE) == 4800);
    sound_play(SND_MOVE);
    sound_mix(out, 64);
    CHECK(fabsf(out[20] - 0.5f * 1.0f) < 0.01f);           /* 16384/32768 at volume 1 */
    for (i = 0; i < 10; i++) sound_mix(out, 1024);
    CHECK(sound_load_wav_mem(SND_TAB, wav, 20) == -2);     /* truncated */
    wav[20] = 3;                                            /* float format: unsupported */
    CHECK(sound_load_wav_mem(SND_TAB, wav, n) == -2);
    CHECK(!sound_is_custom(SND_TAB));
    CHECK(sound_load_wav_file(SND_TAB, "build-host/no_such.wav") == -1);

    /* the app asks for the right sounds */
    {
        AppHost host;
        uint32_t mv, op, cl, tb, ch;
        memset(&host, 0, sizeof(host));
        host.fs_list = fake_none;
        app_init(&g_app, &g_lib, &host);
        run_frames(0, LOAD_MIN_TIME + 1.5f);
        mv = sound_play_count(SND_MOVE); tb = sound_play_count(SND_TAB);
        op = sound_play_count(SND_OPEN); cl = sound_play_count(SND_CLOSE); ch = sound_play_count(SND_CHANGE);
        tap(SFB_RIGHT);
        CHECK(sound_play_count(SND_MOVE) == mv + 1);
        tap(SFB_R1); run_frames(0, 0.5f);
        CHECK(sound_play_count(SND_TAB) == tb + 1);
        tap(SFB_START);
        CHECK(sound_play_count(SND_OPEN) == op + 1);
        tap(SFB_DOWN);
        CHECK(sound_play_count(SND_MOVE) == mv + 2);
        tap(SFB_RIGHT);
        CHECK(sound_play_count(SND_CHANGE) == ch + 1);
        tap(SFB_LEFT);
        tap(SFB_CIRCLE); run_frames(0, 0.4f);
        CHECK(sound_play_count(SND_CLOSE) == cl + 1);
    }
}

/* Minimal PARAM.SFO like PSL1GHT's sfo.py writes: two keys, an int and a string */
static size_t make_sfo(unsigned char *b, uint32_t prio, int sony_fmt)
{
    static const char keys[24] = "ITEM_PRIORITY\0TITLE\0\0\0";   /* 14 + 6 + pad = 24 */
    size_t kts = 20 + 2 * 16, dts = kts + 24, i;
    memset(b, 0, 256);
    b[0] = 0; b[1] = 'P'; b[2] = 'S'; b[3] = 'F';
    b[4] = 1; b[5] = 1;
    b[8] = (unsigned char)kts; b[12] = (unsigned char)dts; b[16] = 2;
    /* entry 0: ITEM_PRIORITY int32 at data offset 0 */
    b[20] = 0; b[22] = sony_fmt ? 4 : 0; b[23] = 4; b[24] = 4; b[28] = 4; b[32] = 0;
    /* entry 1: TITLE utf8 "spookyflow" at data offset 4 */
    b[36] = 14; b[38] = 4; b[39] = 2; b[40] = 11; b[44] = 12; b[48] = 4;
    memcpy(b + kts, keys, 24);
    for (i = 0; i < 4; i++) b[dts + i] = (unsigned char)(prio >> (8 * i));
    memcpy(b + dts + 4, "spookyflow", 11);
    return dts + 16;
}

static void test_sfo(void)
{
    unsigned char b[256];
    uint32_t v = 123;
    size_t n;
    const char *path = "build-host/test_param.sfo";
    FILE *f;
    printf("param.sfo\n");
    n = make_sfo(b, 0, 0);
    CHECK(sfo_get_u32(b, n, "ITEM_PRIORITY", &v) == 0 && v == 0);
    CHECK(sfo_set_u32(b, n, "ITEM_PRIORITY", SFO_PRIORITY_TOP) == 0);
    CHECK(sfo_get_u32(b, n, "ITEM_PRIORITY", &v) == 0 && v == SFO_PRIORITY_TOP);
    CHECK(memcmp(b + 20 + 32 + 24 + 4, "spookyflow", 10) == 0);       /* neighbour untouched */
    CHECK(sfo_get_u32(b, n, "TITLE", &v) == -3);                      /* a string, not an int */
    CHECK(sfo_get_u32(b, n, "MISSING", &v) == -2);
    CHECK(sfo_get_u32(b, n, "ITEM", &v) == -2);                       /* no prefix matches */
    n = make_sfo(b, 7, 1);                                            /* Sony-style 0x0404 */
    CHECK(sfo_get_u32(b, n, "ITEM_PRIORITY", &v) == 0 && v == 7);
    b[1] = 'X';
    CHECK(sfo_get_u32(b, n, "ITEM_PRIORITY", &v) == -1);
    /* on disk: patch in place, size unchanged */
    n = make_sfo(b, 0, 0);
    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) { fwrite(b, 1, n, f); fclose(f); }
    CHECK(sfo_file_set_u32(path, "ITEM_PRIORITY", SFO_PRIORITY_TOP) == 0);
    CHECK(sfo_file_get_u32(path, "ITEM_PRIORITY", &v) == 0 && v == SFO_PRIORITY_TOP);
    f = fopen(path, "rb");
    if (f) { fseek(f, 0, SEEK_END); CHECK((size_t)ftell(f) == n); fclose(f); }
    CHECK(sfo_file_get_u32("build-host/no_such.sfo", "ITEM_PRIORITY", &v) == -4);

    /* the app keeps the file in sync with the setting, at start and on change */
    {
        AppHost host;
        int i;
        CHECK(sfo_file_set_u32(path, "ITEM_PRIORITY", 0) == 0);
        memset(&host, 0, sizeof(host));
        host.fs_list = fake_none;
        host.own_sfo_path = path;
        app_init(&g_app, &g_lib, &host);
        g_app.settings.xmb_top = 0;
        run_frames(0, LOAD_MIN_TIME + 1.5f);
        tap(SFB_START);
        for (i = 0; i < 12; i++) tap(SFB_DOWN);
        CHECK(g_app.rows[g_app.row] == ROW_F_XMB_TOP);
        tap(SFB_CROSS);
        CHECK(g_app.settings.xmb_top == 1);
        CHECK(sfo_file_get_u32(path, "ITEM_PRIORITY", &v) == 0 && v == SFO_PRIORITY_TOP);
        tap(SFB_RIGHT);
        CHECK(g_app.settings.xmb_top == 0);
        CHECK(sfo_file_get_u32(path, "ITEM_PRIORITY", &v) == 0 && v == 0);
        tap(SFB_CIRCLE); run_frames(0, 0.4f);
    }
}

static void run_frames(uint32_t held, float secs)
{
    int n = (int)(secs * 60.0f + 0.5f), i;
    for (i = 0; i < n; i++) app_update(&g_app, held, 1.0f / 60.0f);
}

static void tap(uint32_t b) { app_update(&g_app, b, 1.0f / 60.0f); app_update(&g_app, 0, 1.0f / 60.0f); }

#include "test_covers.h"

static void test_app(void)
{
    AppHost host;
    float t = 0.0f;
    int sel, i, demo_ps1, sync_n1, sync_n3;
    printf("app state machine\n");
    memset(&host, 0, sizeof(host));
    host.fs_list = fake_list;
    app_init(&g_app, &g_lib, &host);
    CHECK(g_app.mode == MODE_LOADING);
    while (g_app.mode == MODE_LOADING && t < LOAD_MIN_TIME + 3.0f) { app_update(&g_app, 0, 1.0f / 60.0f); t += 1.0f / 60.0f; }
    CHECK(g_app.mode == MODE_FLOW);
    CHECK(t >= LOAD_MIN_TIME);                             /* loading never flashes */
    CHECK(!g_lib.tab[PLAT_PS2].demo && g_lib.tab[PLAT_PS2].count == 2);   /* real games shown */
    run_frames(0, 1.0f);
    CHECK(g_app.intro == 1.0f);
    sync_n1 = g_lib.tab[PLAT_PS1].count; sync_n3 = g_lib.tab[PLAT_PS3].count;

    /* same scan on a background thread gives the same library */
    host.run_async = test_run_async;
    app_init(&g_app, &g_lib, &host);
    t = 0.0f;
    while (g_app.mode == MODE_LOADING && t < 1.0f) { app_update(&g_app, 0, 1.0f / 60.0f); t += 1.0f / 60.0f; }
    CHECK(g_tthread_started && g_app.scan_async == 1);
    if (g_tthread_started) { pthread_join(g_tthread, NULL); g_tthread_started = 0; }
    /* the simulated frames run far faster than real time: finish loading now */
    while (g_app.mode == MODE_LOADING && t < LOAD_MIN_TIME + 3.0f) { app_update(&g_app, 0, 1.0f / 60.0f); t += 1.0f / 60.0f; }
    CHECK(g_app.mode == MODE_FLOW);
    CHECK(!g_lib.tab[PLAT_PS2].demo && g_lib.tab[PLAT_PS2].count == 2);
    CHECK(g_lib.tab[PLAT_PS1].count == sync_n1 && g_lib.tab[PLAT_PS3].count == sync_n3);
    host.run_async = NULL;

    /* the loading screen can't be skipped, and says "Doing the Heavy Lifting" once the work is done */
    app_init(&g_app, &g_lib, &host);
    run_frames(0, 0.5f);
    CHECK(g_app.mode == MODE_LOADING && g_app.load_step == LOAD_READY);
    CHECK_STR(g_app.load_status, "Doing the Heavy Lifting");
    CHECK(g_app.load_shown < 0.5f);                        /* bar paced over the minimum time */
    tap(SFB_CROSS); tap(SFB_START); tap(SFB_CIRCLE); tap(SFB_TRIANGLE);
    CHECK(g_app.mode == MODE_LOADING);
    run_frames(0, LOAD_MIN_TIME);
    CHECK(g_app.mode == MODE_FLOW);
    run_frames(0, 1.0f);

    /* demo fallback when nothing is on the HDD */
    host.fs_list = fake_none;
    app_init(&g_app, &g_lib, &host);
    run_frames(0, LOAD_MIN_TIME + 1.5f);   /* loading screen + intro */
    CHECK(g_app.mode == MODE_FLOW);
    CHECK(g_lib.tab[PLAT_PS1].demo && g_lib.tab[PLAT_PS1].count >= 60);
    CHECK(g_lib.tab[PLAT_PS2].count >= 60 && g_lib.tab[PLAT_PS3].count >= 60);
    demo_ps1 = g_lib.tab[PLAT_PS1].count;
    (void)demo_ps1;

    /* scrolling, per-tab memory */
    CHECK(g_app.tab == PLAT_PS2);
    tap(SFB_RIGHT); tap(SFB_RIGHT); run_frames(0, 0.6f);
    CHECK(g_app.view[PLAT_PS2].sel == 2);
    CHECK(fabsf(g_app.view[PLAT_PS2].scroll.x - 2.0f) < 0.01f);
    tap(SFB_R1); run_frames(0, 0.5f);
    CHECK(g_app.tab == PLAT_PS3);
    tap(SFB_RIGHT); run_frames(0, 0.4f);
    tap(SFB_L1); run_frames(0, 0.5f);
    CHECK(g_app.tab == PLAT_PS2 && g_app.view[PLAT_PS2].sel == 2 && g_app.view[PLAT_PS3].sel == 1);
    tap(SFB_L1); run_frames(0, 0.5f);
    CHECK(g_app.tab == PLAT_PS1);
    tap(SFB_L1); run_frames(0, 0.5f);
    CHECK(g_app.tab == PLAT_PS1 && g_app.tab_anim >= 1.0f);  /* L2 on PS1: stays, no wrap */
    tap(SFB_R1); tap(SFB_R1); run_frames(0, 0.5f);
    CHECK(g_app.tab == PLAT_PS3);
    tap(SFB_R1); run_frames(0, 0.5f);
    CHECK(g_app.tab == PLAT_PS3 && g_app.tab_anim >= 1.0f);  /* R2 on PS3: stays, no wrap */
    tap(SFB_L1); run_frames(0, 0.5f);
    CHECK(g_app.tab == PLAT_PS2);

    /* holding right moves exactly one game (no auto-repeat in the flow) */
    sel = g_app.view[PLAT_PS2].sel;
    run_frames(SFB_RIGHT, 1.5f);
    CHECK(g_app.view[PLAT_PS2].sel - sel == 1);
    run_frames(0, 0.1f);
    run_frames(SFB_LEFT, 1.5f);
    CHECK(g_app.view[PLAT_PS2].sel == sel);
    tap(SFB_RIGHT); tap(SFB_RIGHT);
    CHECK(g_app.view[PLAT_PS2].sel - sel == 2);
    run_frames(0, 0.2f);
    /* the left stick still scrolls with accelerating repeat while held */
    sel = g_app.view[PLAT_PS2].sel;
    run_frames(SFB_STICK_RIGHT, 1.5f);
    CHECK(g_app.view[PLAT_PS2].sel - sel > 15);
    run_frames(0, 0.2f);
    sel = g_app.view[PLAT_PS2].sel;
    run_frames(SFB_STICK_LEFT, 0.8f);
    CHECK(sel - g_app.view[PLAT_PS2].sel > 3);
    run_frames(0, 0.5f);

    /* R1 on the last letter / L1 at the very first game: no wrap, no overlay */
    {
        const GameList *gl2 = &g_lib.tab[PLAT_PS2];
        int keep = g_app.view[PLAT_PS2].sel;
        app_select(&g_app, PLAT_PS2, gl2->count - 1, 0); run_frames(0, 1.5f);
        tap(SFB_R2); run_frames(0, 0.1f);
        CHECK(g_app.view[PLAT_PS2].sel == gl2->count - 1);
        CHECK(g_app.letter_t >= LETTER_TIME);
        app_select(&g_app, PLAT_PS2, 0, 0); run_frames(0, 1.5f);
        tap(SFB_L2); run_frames(0, 0.1f);
        CHECK(g_app.view[PLAT_PS2].sel == 0);
        CHECK(g_app.letter_t >= LETTER_TIME);
        app_select(&g_app, PLAT_PS2, keep, 0); run_frames(0, 1.5f);
    }

    /* letter jump shows overlay */
    tap(SFB_R2);
    CHECK(g_app.letter_t < 0.1f && g_app.letter[0] != '\0');
    run_frames(0, 1.2f);
    CHECK(g_app.letter_t >= LETTER_TIME);

    /* launch: no pop-up, but the launch is recorded */
    g_app.toast_t = TOAST_TIME;
    g_app.last_launch[0] = '\0';
    tap(SFB_CROSS);
    CHECK(g_app.last_launch[0] != '\0' && g_app.toast_t >= TOAST_TIME);

    /* game settings panel; ◯ closes it and must NOT arm the exit hold */
    tap(SFB_TRIANGLE);
    CHECK(g_app.panel == PANEL_GAME && g_app.nrows == 5);
    run_frames(0, 0.3f);
    tap(SFB_DOWN); tap(SFB_DOWN); tap(SFB_DOWN);
    CHECK(g_app.rows[g_app.row] == ROW_G_RSX);
    tap(SFB_RIGHT);
    CHECK(g_app.panel_cfg.rsx == 1);
    CHECK(settings_game_get(&g_app.settings, g_app.panel_cfg.key).rsx == 1);
    app_update(&g_app, SFB_CIRCLE, 1.0f / 60.0f);          /* press closes panel... */
    run_frames(SFB_CIRCLE, 2.5f);                           /* ...and keeps holding */
    CHECK(!g_app.exiting && !g_app.quit);
    CHECK(g_app.panel == PANEL_NONE);
    run_frames(0, 0.1f);

    /* flow settings: ignore "The" re-sorts but keeps the selected game */
    {
        char key[SET_KEY_MAX];
        sf_strlcpy(key, lib_key(&g_lib, app_selected_game(&g_app, PLAT_PS2)), sizeof(key));
        tap(SFB_START);
        CHECK(g_app.panel == PANEL_FLOW && g_app.nrows == 16);
        for (i = 0; i < 11; i++) tap(SFB_DOWN);
        CHECK(g_app.rows[g_app.row] == ROW_F_IGNORE_THE);
        tap(SFB_CROSS);
        CHECK(g_app.settings.ignore_the == 1);
        CHECK_STR(lib_key(&g_lib, app_selected_game(&g_app, PLAT_PS2)), key);
        run_frames(0, 0.5f);
        CHECK(fabsf(g_app.list_scroll.x - 6.0f) < 0.01f);   /* row 12 of 16: scrolled by six */
        tap(SFB_DOWN); tap(SFB_DOWN); tap(SFB_DOWN); run_frames(0, 0.5f);
        CHECK(g_app.rows[g_app.row] == ROW_F_QUIT);
        CHECK(fabsf(g_app.list_scroll.x - 9.0f) < 0.01f);   /* scrolled to keep row 15 visible */
        /* the menus ignore the stick, and holding the D-pad moves only one row */
        run_frames(SFB_STICK_UP, 1.0f); run_frames(0, 0.1f);
        CHECK(g_app.rows[g_app.row] == ROW_F_QUIT);
        run_frames(SFB_STICK_LEFT, 1.0f); run_frames(SFB_STICK_RIGHT, 1.0f); run_frames(0, 0.1f);
        CHECK(g_app.settings.ignore_the == 1 && g_app.settings.autoplay == 0);
        run_frames(SFB_UP, 1.5f); run_frames(0, 0.1f);
        CHECK(g_app.rows[g_app.row] == ROW_F_RESCAN);
        tap(SFB_DOWN);
        /* power off needs a second press within the window; moving away cancels */
        tap(SFB_DOWN);
        CHECK(g_app.rows[g_app.row] == ROW_F_POWEROFF);
        tap(SFB_CROSS);
        CHECK(!g_app.poweroff && !g_app.exiting && g_app.poweroff_arm > 0.0f);
        run_frames(0, POWEROFF_CONFIRM_TIME + 0.2f);
        CHECK(g_app.poweroff_arm == 0.0f);
        tap(SFB_CROSS); tap(SFB_UP); tap(SFB_DOWN);
        CHECK(g_app.poweroff_arm == 0.0f);
        /* without a power-off provider (no webMAN): nothing happens but a message */
        g_app.host.power_off = NULL;
        tap(SFB_CROSS); tap(SFB_CROSS);
        CHECK(!g_app.poweroff && !g_app.exiting && g_app.toast_t < 0.1f);
        g_app.host.power_off = power_off_ok;
        tap(SFB_CROSS); tap(SFB_CROSS);
        CHECK(g_app.poweroff && g_app.exiting && g_power_off_calls == 1);
        run_frames(0, EXIT_FADE_TIME + 0.2f);
        CHECK(g_app.quit);
        app_init(&g_app, &g_lib, &host);
        run_frames(0, LOAD_MIN_TIME + 1.5f);
        tap(SFB_CIRCLE); run_frames(0, 0.4f);
        CHECK(g_app.panel == PANEL_NONE);
    }

    /* tap ◯ on the main screen: nothing; hold: exit after 1.5 s then fade */
    tap(SFB_CIRCLE); run_frames(0, 0.2f);
    CHECK(!g_app.exiting);
    run_frames(SFB_CIRCLE, 1.0f);
    CHECK(!g_app.exiting && g_app.hold.shown > 0.5f);
    run_frames(SFB_CIRCLE, 0.6f);
    CHECK(g_app.exiting);
    CHECK(!g_app.quit);
    run_frames(0, EXIT_FADE_TIME + 0.1f);
    CHECK(g_app.quit && g_app.exit_fade >= 1.0f);

    /* Quit row and request_exit follow the same path */
    host.fs_list = fake_none;
    app_init(&g_app, &g_lib, &host);
    run_frames(0, LOAD_MIN_TIME + 1.5f);   /* loading screen + intro */
    tap(SFB_START);
    for (i = 0; i < 16; i++) tap(SFB_DOWN);
    tap(SFB_UP);                                           /* last row is Power off */
    CHECK(g_app.rows[g_app.row] == ROW_F_QUIT);
    tap(SFB_CROSS);
    CHECK(g_app.exiting && !g_app.poweroff);
    run_frames(0, 0.5f);
    CHECK(g_app.quit);

    /* rescan returns to the loading screen and back */
    app_init(&g_app, &g_lib, &host);
    run_frames(0, LOAD_MIN_TIME + 1.5f);   /* loading screen + intro */
    tap(SFB_START);
    for (i = 0; i < 13; i++) tap(SFB_DOWN);
    CHECK(g_app.rows[g_app.row] == ROW_F_RESCAN);
    tap(SFB_CROSS);
    CHECK(g_app.mode == MODE_LOADING && g_app.panel == PANEL_NONE);
    run_frames(0, LOAD_MIN_TIME + 1.5f);   /* loading screen + intro */
    CHECK(g_app.mode == MODE_FLOW);
}

static void test_font_and_draw(void)
{
    static unsigned char buf[UI_SCRATCH_BYTES];
    unsigned char out[8];
    static const unsigned char bad[] = { 5, 1 };          /* run of 6 into 4-byte buffer */
    Screen s = screen_ps3(1280, 720);
    extern long g_null_vertices;
    long v0;
    printf("font / textures / draw\n");
    CHECK(font_decode_atlas(&g_font_main, buf, sizeof(buf)) == 0);
    CHECK(font_decode_atlas(&g_font_big, buf, sizeof(buf)) == 0);
    CHECK(ui_upload_textures(buf, sizeof(buf)) == 0);
    CHECK(sf_rle_decode(bad, 2, out, 4) == -1);
    CHECK(font_find(&g_font_main, 'A') != NULL && font_find(&g_font_main, 0x2026) != NULL);
    CHECK(font_find(&g_font_main, 0x4E00) == NULL && font_glyph(&g_font_main, 0x4E00) != NULL);
    CHECK(font_find(&g_font_big, 'Z') && font_find(&g_font_big, '#'));
    d_set_screen(&s);
    CHECK(d_text_width(&g_font_main, 20.0f, "abc") > 0.0f);
    CHECK(icons_aspect(ICON_CROSS) == 1.0f && icons_aspect(ICON_L1) > 1.2f);
    /* a full frame draws within a sane vertex budget (Tiny3D buffer) */
    {
        AppHost host;
        memset(&host, 0, sizeof(host));
        host.fs_list = fake_none;
        app_init(&g_app, &g_lib, &host);
        run_frames(0, LOAD_MIN_TIME + 1.5f);   /* loading screen + intro */
        v0 = g_null_vertices;
        ui_draw(&g_app);
        printf("  vertices per flow frame: %ld\n", g_null_vertices - v0);
        CHECK(g_null_vertices - v0 > 1000 && g_null_vertices - v0 < 60000);
        tap(SFB_START); run_frames(0, 0.3f);
        v0 = g_null_vertices;
        ui_draw(&g_app);
        printf("  vertices per panel frame: %ld\n", g_null_vertices - v0);
        CHECK(g_null_vertices - v0 < 60000);
    }
}

int main(void)
{
    log_set_echo(0);
    test_util();
    test_title_clean();
    test_sort();
    test_letters();
    test_spring();
    test_layout();
    test_vertical_spacing();
    test_bar_layout();
    test_title_fit();
    test_settings();
    test_menu();
    test_input_repeat();
    test_hold_exit();
    test_cover_cache();
    test_scanner();
    test_app();
    test_sfo();
    test_sound();
    test_imgscale();
    test_font_and_draw();
    test_discid();
    test_cover_lookup();
    test_cover_process();
    test_cover_loader();
    test_cover_cache_gpu();
    test_app_covers();
    test_sleep_and_info();
    test_cover_cache_tabs();
    test_case_images();
    test_multi_disc();
    test_multi_disc_names();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
