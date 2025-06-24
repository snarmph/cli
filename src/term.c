#include "term.h"

#include <Windows.h>

#include "colla/pretty_print.h"

#define TERM_DEFAULT_OUTPUT os_stdout()
#define TERM_DEFAULT_FPS    60

#define TERM_VT(...) WriteConsole((HANDLE)term.output.data, TEXT("\x1b" __VA_ARGS__), arrlen(TEXT("\x1b" __VA_ARGS__)), NULL, NULL)

#define TERM_SAVE_POS() TERM_VT("7")
#define TERM_LOAD_POS() TERM_VT("8")
#define TERM_GET_POS()  TERM_VT("[6n")
#define TERM_MOV_UP(yoff)    TERM_VT("[" # yoff "A")
#define TERM_MOV_DOWN(yoff)  TERM_VT("[" # yoff "B")
#define TERM_MOV_RIGHT(xoff) TERM_VT("[" # xoff "C")
#define TERM_MOV_LEFT(xoff)  TERM_VT("[" # xoff "D")

#define TERM_CLEAR_SCREEN() TERM_VT("[2J")
#define TERM_HIDE_CURSOR() TERM_VT("[?25l")
#define TERM_SHOW_CURSOR() TERM_VT("[?25h")
#define TERM_ERASE_SCREEN() TERM_VT("[2J")
#define TERM_ERASE_LINE() TERM_VT("[2K")
#define TERM_ERASE_REST_OF_LINE() TERM_VT("[K")
#define TERM_CURSOR_HOME_POS() TERM_VT("[H")
#define TERM_SET_WINDOW_TITLE(title) TERM_VT("]2;" title "\x07")

#define TERM_ERASE_N_CHARS(n) print("\x1b[%zuX", (n));

typedef struct ticker_t ticker_t;
struct ticker_t {
    int fps;
    float dt;
    i64 accumulated;
    i64 ticks;
    i64 prev;

    void (*update)(float dt, void *userdata);
    void *userdata;
};

ticker_t ticker_init(int fps, void (*update)(float dt, void *userdata), void *userdata);
void ticker_tick(ticker_t *ctx);

i64 term__get_ticks_per_second(void);
i64 term__get_ticks(void);

typedef struct term_pos_t term_pos_t;
struct term_pos_t {
    int x;
    int y;
};

typedef struct term_t term_t;
struct term_t {
    arena_t arena;
    arena_t frame_arenas[2];
    int cur_arena;
    bool fullscreen;

    oshandle_t output;

    bool should_quit;

    termapp_t app;
    ticker_t ticker;

    str_t last_render;
    strview_t last_render_lines[128];
    int last_lines_count;

    int width;
    int height;

    int fps;
    float dt;
};

term_t term = {0};

void term__update_internal(float dt, void *userdata);
bool term__has_input(void);
void term__poll_input(void);
void term__cleanup(void);

term_pos_t term__get_cursor_pos(void);

//////////////////////////////////////

void term_init(termdesc_t *desc) {
    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    term.arena = arena;
    for (int i = 0; i < arrlen(term.frame_arenas); ++i) {
        term.frame_arenas[i] = arena_make(ARENA_VIRTUAL, GB(1));
    }
    term.output = TERM_DEFAULT_OUTPUT;
    term.last_lines_count = 0;
    
    int fps = TERM_DEFAULT_FPS;

    if (!desc) {
        fatal("desc needed for term_init");
    }
    
    if (os_handle_valid(desc->output)) {
        term.output = desc->output;
    }

    if (desc->fps >= 1) {
        fps = desc->fps;
    }

    term.ticker = ticker_init(fps, term__update_internal, &term);

    term.app = desc->app;

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    term.output.data = (uptr)handle;

    DWORD console_mode = 0;
    if (!GetConsoleMode(handle, &console_mode)) {
        fatal("couldn't get console mode: %v", os_get_error_string(os_get_last_error()));
    }

    console_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(handle, console_mode);

    TERM_HIDE_CURSOR();
    TERM_SAVE_POS();
    if (desc->fullscreen) {
        TERM_CLEAR_SCREEN();
    }

    term.fullscreen = desc->fullscreen;
}

void term_run(void) {
    // setup input and save previous state
    oshandle_t input = os_stdin();

    DWORD prev = 0; // 0x01f7
    GetConsoleMode((HANDLE)input.data, &prev);

    SetConsoleMode((HANDLE)input.data, ENABLE_VIRTUAL_TERMINAL_INPUT);

    // render view once at the beginning
    str_t first_view = term.app.view(&term.frame_arenas[term.cur_arena], term.app.userdata);
    term_write(strv(first_view));

    term.cur_arena = (term.cur_arena + 1) % arrlen(term.frame_arenas);

    while (!term.should_quit) {
        ticker_tick(&term.ticker);
    }

    term__cleanup();
    SetConsoleMode((HANDLE)input.data, (DWORD)(~ENABLE_VIRTUAL_TERMINAL_INPUT));
}

void term_move(int offx, int offy) {
    if (offx > 0) print("\x1b[%dC", offx);
    else if (offx < 0) print("\x1b[%dD", -offx);

    if (offy > 0) print("\x1b[%dB", offy);
    else if (offy < 0) print("\x1b[%dA", -offy);
}

void term_write(strview_t str) {
    instream_t in = istr_init(str);

    if (term.fullscreen) {
        TERM_CURSOR_HOME_POS();
    }

    if (term.last_lines_count) {
        term_move(0, -(term.last_lines_count - 1));
        TERM_SAVE_POS();
    }

    int line_count = 0;
    while (!istr_is_finished(&in)) {
        strview_t line = istr_get_line(&in);
        if (line_count >= arrlen(term.last_render_lines)) {
            fatal("TODO: too many lines");
        }

        strview_t prev = term.last_render_lines[line_count];
        bool has_newline = istr_prev(&in) == '\n';
        bool is_same = strv_equals(line, prev);

        term_pos_t pos_before = term__get_cursor_pos();

        if (!is_same) {
            print("\r");
            pretty_print(term.arena, "%v", line);
            TERM_ERASE_REST_OF_LINE();
        }

        term_pos_t pos_after = term__get_cursor_pos();

        pretty_print(term.arena, "(%d:%d)|(%d:%d)", pos_before.x, pos_before.y, pos_after.x, pos_after.y);
        if (pos_before.y != pos_after.y) {
            line_count += pos_after.y - pos_before.y;
        }

        if (has_newline) {
            if (is_same) {
                TERM_MOV_DOWN(1);
            }
            else {
                print("\n");
            }
        }

        if (!istr_is_finished(&in)) {
            //print("\n");
        }
        //TERM_MOV_DOWN(1);

        term.last_render_lines[line_count] = line;
        line_count++;
    }
    int diff_count = term.last_lines_count - line_count;

    for (int i = 0; i < diff_count; ++i) {
        print("\r");
        TERM_ERASE_LINE();
        TERM_MOV_DOWN(1);
    }    

#if 0
    int new_count = line_count - term.last_lines_count;
    if (new_count > 0) {
         term_move(0, (-new_count) + 1);
         TERM_SAVE_POS();
    }
#endif
   
    term.last_lines_count = line_count;
}

/// COMPONENTS //////////////////////////////////////////

// -- spinner ---------------------------------------- //

typedef struct spinner_data_t spinner_data_t;
struct spinner_data_t {
    strview_t frames[16];
    int count;
    float time;
};


spinner_data_t spinner__data[SPINNER__COUNT] = {0};
bool spinner__data_init = false;

spinner_t spinner_init(spinner_type_e type) {
    if (!spinner__data_init) {
#define INIT(t, ...) (spinner_data_t){ .time = (t), .frames = { __VA_ARGS__ }, .count = arrlen(((strview_t[]){ __VA_ARGS__ })) }

        spinner__data[SPINNER_LINE]     = INIT(0.10f, cstrv("|"), cstrv("/"), cstrv("-"), cstrv("\\"));
        spinner__data[SPINNER_DOT]      = INIT(0.10f, cstrv("⣾"), cstrv("⣽"), cstrv("⣻"), cstrv("⢿"), cstrv("⡿"), cstrv("⣟"), cstrv("⣯"), cstrv("⣷"));
        spinner__data[SPINNER_MINIDOT]  = INIT(0.08f, cstrv("⠋"), cstrv("⠙"), cstrv("⠹"), cstrv("⠸"), cstrv("⠼"), cstrv("⠴"), cstrv("⠦"), cstrv("⠧"), cstrv("⠇"), cstrv("⠏"));
        spinner__data[SPINNER_JUMP]     = INIT(0.10f, cstrv("⢄"), cstrv("⢂"), cstrv("⢁"), cstrv("⡁"), cstrv("⡈"), cstrv("⡐"), cstrv("⡠"));
        spinner__data[SPINNER_PULSE]    = INIT(0.12f, cstrv("█"), cstrv("▓"), cstrv("▒"), cstrv("░"));
        spinner__data[SPINNER_POINTS]   = INIT(0.14f, cstrv("∙∙∙"), cstrv("●∙∙"), cstrv("∙●∙"), cstrv("∙∙●"));
        spinner__data[SPINNER_GLOBE]    = INIT(0.25f, cstrv("🌍"), cstrv("🌎"), cstrv("🌏"));
        spinner__data[SPINNER_MOON]     = INIT(0.12f, cstrv("🌑"), cstrv("🌒"), cstrv("🌓"), cstrv("🌔"), cstrv("🌕"), cstrv("🌖"), cstrv("🌗"), cstrv("🌘"));
        spinner__data[SPINNER_METER]    = INIT(0.14f, cstrv("▱▱▱"), cstrv("▰▱▱"), cstrv("▰▰▱"), cstrv("▰▰▰"), cstrv("▰▰▱"), cstrv("▰▱▱"), cstrv("▱▱▱"));
        spinner__data[SPINNER_ELLIPSIS] = INIT(0.33f, cstrv(""), cstrv("."), cstrv(".."), cstrv("..."));

#undef INIT
    }

    return (spinner_t) {
        .frames = spinner__data[type].frames,
        .count = spinner__data[type].count,
        .timer = spinner__data[type].time,
    };
}

void spinner_update(spinner_t *ctx, float dt) {
    ctx->passed += dt;
    while (ctx->passed >= ctx->timer) {
        ctx->passed -= ctx->timer;
        ctx->cur = (ctx->cur + 1) % ctx->count;
    }
}

//////////////////////////////////////

ticker_t ticker_init(int fps, void (*update)(float dt, void *userdata), void *userdata) {
    i64 tps = term__get_ticks_per_second();

    return (ticker_t) {
        .fps = fps,
        .dt = 1.f / (float)fps,
        .ticks = (i64)((float)tps / (float)fps),
        .prev = term__get_ticks(),
        .update = update,
        .userdata = userdata,
    };

}

void ticker_tick(ticker_t *ctx) {
    i64 now = term__get_ticks();
    i64 passed = now - ctx->prev;
    if (passed >= ctx->ticks) {
        ctx->prev = now;

        while (passed >= ctx->ticks) {
            passed -= ctx->ticks;
            ctx->update(ctx->dt, ctx->userdata);
        }

        ctx->accumulated += passed;

        while (ctx->accumulated >= ctx->ticks) {
            ctx->accumulated -= ctx->ticks;
            ctx->update(ctx->dt, ctx->userdata);
        }
    }
}

i64 term__get_ticks_per_second(void) {
    LARGE_INTEGER tps = {0};
    if (!QueryPerformanceFrequency(&tps)) {
        fatal("failed to query performance frequency");
    }
    return tps.QuadPart;
}

i64 term__get_ticks(void) {
    LARGE_INTEGER ticks = {0};
    if (!QueryPerformanceCounter(&ticks)) {
        fatal("failed to query performance counter");
    }
    return ticks.QuadPart;
}

void term__update_internal(float dt, void *userdata) {
    COLLA_UNUSED(userdata);

    term.dt = dt;

    term.cur_arena = (term.cur_arena + 1) % arrlen(term.frame_arenas);
    arena_t *frame_arena = &term.frame_arenas[term.cur_arena];

    term__poll_input();

    arena_rewind(frame_arena, 0);

    term.should_quit = term.app.update(frame_arena, dt, term.app.userdata);
    str_t str = term.app.view(frame_arena, term.app.userdata);

    term_write(strv(str));
}

bool term__has_input(void) {
    oshandle_t conin = os_stdin();

    INPUT_RECORD rec;
    DWORD read;
    
    while (PeekConsoleInput((HANDLE)conin.data, &rec, 1, &read) && read > 0) {
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            return true;
        }

        ReadConsoleInput((HANDLE)conin.data, &rec, 1, &read);
    }

    return false;
}

typedef enum {
    VT_STATE_BEGIN,
    VT_STATE_ESC,
    VT_STATE_CSI,   // control sequence introduces ([)
    VT_STATE_OSC,   // operating system command (])
    VT_STATE_DCS,   // device control string (P)
    VT_STATE_SS3,   // single shift 3 (O)
    VT_STATE_MOD,   // modifier (;)
    VT_STATE__COUNT,
} term_vt_state_e;

typedef struct term_vt_parser_t term_vt_parser_t;
struct term_vt_parser_t {
    term_vt_state_e state;
    int num;
    int modifier;
};

void term_send_key_event(strview_t key, strview_t mod) {
    arena_t *arena = &term.frame_arenas[term.cur_arena];
    str_t value = STR_EMPTY;
    if (strv_is_empty(mod)) {
        value = str(arena, key);
    }
    else {
        value = str_fmt(arena, "%v+%v", mod, key);
    }

    term.app.event(
        &(termevent_t){
            .type = TERM_EVENT_KEY,
            .is_key_down = true,
            .value = value,
        },
        term.app.userdata
    );
}

void term_vt_process(term_vt_parser_t *ctx, char c) {
    static strview_t keys[] = {
        [2]   = cstrv("insert"),
        [3]   = cstrv("delete"),
        [5]   = cstrv("page-up"),
        [6]   = cstrv("page-down"),
        [15]  = cstrv("F5"),
        [17]  = cstrv("F6"),
        [18]  = cstrv("F7"),
        [19]  = cstrv("F8"),
        [20]  = cstrv("F9"),
        [21]  = cstrv("F10"),
        [23]  = cstrv("F11"),
        [24]  = cstrv("F12"),

        ['A'] = cstrv("up"),
        ['B'] = cstrv("down"),
        ['C'] = cstrv("right"),
        ['D'] = cstrv("left"),
        ['H'] = cstrv("home"),
        ['F'] = cstrv("end"),
 
        ['P'] = cstrv("F1"),
        ['Q'] = cstrv("F2"),
        ['R'] = cstrv("F3"),
        ['S'] = cstrv("F4"),
    };

    static strview_t mod[] = {
        [2] = cstrv("shift"),
        [3] = cstrv("alt"),
        [4] = cstrv("shift+alt"),
        [5] = cstrv("ctrl"),
        [6] = cstrv("shift+ctrl"),
        [7] = cstrv("alt+ctrl"),
        [8] = cstrv("shift+alt+ctrl"),
    };

    switch (ctx->state) {
        case VT_STATE_BEGIN:
            memset(ctx, 0, sizeof(*ctx));

            // escape
            if (c == '\x1b') {
                ctx->state = VT_STATE_ESC;
            }
            // tab
            else if (c == '\t') {
                term_send_key_event(strv("tab"), mod[ctx->modifier]);
            }
            // backspace
            else if (c == '\x7f') {
                term_send_key_event(strv("backspace"), mod[ctx->modifier]);
            }
            // enter
            else if (c == '\x0d') {
                term_send_key_event(strv("enter"), mod[ctx->modifier]);
            }
            else {
                term_send_key_event(strv(&c, 1), mod[ctx->modifier]);
            }
            break;

        case VT_STATE_ESC:
            if (c == '[') {
                ctx->state = VT_STATE_CSI;
            }
            else if (c == 'O') {
                ctx->state = VT_STATE_SS3;
            }
            break;
            
        case VT_STATE_CSI:
            switch (c) {
                case '~':
                    term_send_key_event(keys[ctx->num], mod[ctx->modifier]);
                    ctx->state = VT_STATE_BEGIN;
                    break;

                case ';':
                    ctx->state = VT_STATE_MOD;
                    break;
                
                default:
                    if (char_is_num(c)) {
                        ctx->num = (ctx->num * 10) + (c - '0');
                    }
                    else if (char_is_alpha(c)) {
                        term_send_key_event(keys[c], mod[ctx->modifier]);
                        ctx->state = VT_STATE_BEGIN;
                    }
                    break;
            }
            break;

        case VT_STATE_OSC:
            break;

        case VT_STATE_DCS:
            break;

        case VT_STATE_SS3:
            term_send_key_event(keys[c], mod[ctx->modifier]);
            ctx->state = VT_STATE_BEGIN;
            break;

        case VT_STATE_MOD:
            if (char_is_num(c)) {
                ctx->modifier = c - '0';
                ctx->state = VT_STATE_CSI;
            }
            break;

        default: break;
    }
}

void term__poll_input(void) {
    oshandle_t conin = os_stdin();

    if (!term__has_input()) {
        return;
    }

    u8 buffer[16];
    usize read = os_file_read(conin, buffer, sizeof(buffer)); 
    if (read == 0) {
        term.should_quit = true;
    }

    term.should_quit = false;

    term_vt_parser_t vt = {0};
    for (usize i = 0; i < read; ++i) {
        term_vt_process(&vt, (char)buffer[i]);
    }

    if (vt.state == VT_STATE_ESC) {
        term_send_key_event(strv("escape"), STRV_EMPTY);
    }
    else if (vt.state != VT_STATE_BEGIN) {
        colla_assert(false);
    }
}

void term__cleanup(void) {
    if (term.fullscreen) {
        TERM_CLEAR_SCREEN();
    }
    TERM_SHOW_CURSOR();
    arena_cleanup(&term.arena);
}

term_pos_t term__get_cursor_pos(void) {
    term_pos_t pos = {0};

    oshandle_t conin = os_stdin();

    TERM_GET_POS();
    // FlushConsoleInputBuffer((HANDLE)conin.data);

    if (!term__has_input()) {
        return pos;
    }

    u8 buffer[16];
    usize read = os_file_read(conin, buffer, sizeof(buffer));
    if (read == 0) {
        fatal("couldn't get terminal position");
    }
    // ESC [ row ; col R

    instream_t in = istr_init(strv((char*)buffer, read));

    if (istr_get(&in) != '\x1b') {
        fatal("first char is not ESC: 0x%02x", istr_prev(&in));
    }

    if (istr_get(&in) != '[') {
        fatal("first char is not [: 0x%02x", istr_prev(&in));
    }

    strview_t bef = strv(in.cur, istr_tell(&in));

    i32 row, col;
    if (!istr_get_i32(&in, &row)) {
        fatal("could not get row: (%v)", bef);
    }
    istr_skip(&in, 1);
    if (!istr_get_i32(&in, &col)) {
        fatal("could not get col: (%v)", bef);
    }

    return (term_pos_t){ col, row };
}
