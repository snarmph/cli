#include "colla/build.c"

#include "term.c"

#if 0
i64 get_ticks_per_second(void) {
    LARGE_INTEGER tps = {0};
    if (!QueryPerformanceFrequency(&tps)) {
        fatal("failed to query performance frequency");
    }
    return tps.QuadPart;
}

i64 get_ticks(void) {
    LARGE_INTEGER ticks = {0};
    if (!QueryPerformanceCounter(&ticks)) {
        fatal("failed to query performance counter");
    }
    return ticks.QuadPart;
}

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

ticker_t ticker_init(int fps, void (*update)(float dt, void *userdata), void *userdata) {
    i64 tps = get_ticks_per_second();

    return (ticker_t) {
        .fps = fps,
        .dt = 1.0 / (float)fps,
        .ticks = (float)tps / (float)fps,
        .prev = get_ticks(),
        .update = update,
        .userdata = userdata,
    };
}

void ticker_tick(ticker_t *ctx) {
    i64 now = get_ticks();
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

#define TERM_DEFAULT_OUTPUT os_stdout()
#define TERM_DEFAULT_FPS    60

typedef struct term_t term_t;
struct term_t {
    arena_t arena;
    arena_t frame_arena;
    arena_t swap_arena;
    bool swap;
    bool fullscreen;

    oshandle_t sync_mtx;
    oshandle_t wakeup_cond;
    oshandle_t output;

    volatile long is_dirty;
    volatile long should_quit;

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

#define TERM_VT(...) WriteConsole((HANDLE)term.output.data, TEXT("\x1b" __VA_ARGS__), arrlen(TEXT("\x1b" __VA_ARGS__)), NULL, NULL)

#define TERM_SAVE_POS() TERM_VT("7")
#define TERM_LOAD_POS() TERM_VT("8")
#define TERM_MOV_UP(yoff)    TERM_VT("[" # yoff "A")
#define TERM_MOV_DOWN(yoff)  TERM_VT("[" # yoff "B")
#define TERM_MOV_RIGHT(xoff) TERM_VT("[" # xoff "C")
#define TERM_MOV_LEFT(xoff)  TERM_VT("[" # xoff "D")

#define TERM_CLEAR_SCREEN() TERM_VT("[2J")
#define TERM_HIDE_CURSOR() TERM_VT("[?25l")
#define TERM_SHOW_CURSOR() TERM_VT("[?25h")
#define TERM_ERASE_SCREEN() TERM_VT("[2J")
#define TERM_ERASE_LINE() TERM_VT("[2K")
#define TERM_CURSOR_HOME_POS() TERM_VT("[H")
#define TERM_SET_WINDOW_TITLE(title) TERM_VT("]2;" title "\x07")

#define TERM_ERASE_N_CHARS(n) print("\x1b[%zuX", (n));

void term_move(int x, int y) {
    if (x > 0) print("\x1b[%dC", x);
    else if (x < 0) print("\x1b[%dD", -x);

    if (y > 0) print("\x1b[%dB", y);
    else if (y < 0) print("\x1b[%dA", -y);
}

void term_write(strview_t str) {
    instream_t in = istr_init(str);

    TERM_LOAD_POS();

    if (term.fullscreen) {
        TERM_CURSOR_HOME_POS();
    }

    int line_count = 0;
    while (!istr_is_finished(&in)) {
        strview_t line = istr_get_line(&in);
        if (line_count >= arrlen(term.last_render_lines)) {
            fatal("TODO: too many lines");
        }

        strview_t prev = term.last_render_lines[line_count];

        if (!strv_equals(line, prev)) { 
            print("\r");
            pretty_print(term.frame_arena, "%v", line);
            
            if (line.len < prev.len) {
                TERM_ERASE_N_CHARS(prev.len - line.len);
            }
        }

        if (line_count > term.last_lines_count) {
            print("\n");
        }
        else {
            TERM_MOV_DOWN(1);
        }

        term.last_render_lines[line_count] = line;
        line_count++;
    }

    int new_count = line_count - term.last_lines_count;
    int diff_count = term.last_lines_count - line_count;

    for (int i = 0; i < diff_count; ++i) {
        print("\r");
        TERM_ERASE_LINE();
        TERM_MOV_DOWN(1);
    }
    
    if (new_count > 0) {
         term_move(0, -new_count);
         TERM_SAVE_POS();
    }
    
    term.last_lines_count = line_count;
}

bool term_has_input(void) {
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


void term__poll_input(term_t *tm) {
    oshandle_t conin = os_stdin();

    if (!term_has_input()) {
        return;
    }

    u8 buffer[1024];
    usize read = os_file_read(conin, buffer, sizeof(buffer)); 
    if (read == 0) {
        term.should_quit = true;
    }

    bool should_quit = false;

    for (usize i = 0; i < read; ++i) {
        termevent_t e = {
            .is_key_down = true,
            .type = TERM_EVENT_KEY,
        };

        if (buffer[i] == '\x1b') {
            assert(buffer[i + 1] == '[');

            termkey_e keys[128] = {
                ['A'] = TERM_KEY_UP,
                ['B'] = TERM_KEY_DOWN,
                ['C'] = TERM_KEY_RIGHT,
                ['D'] = TERM_KEY_LEFT,
            };

            strview_t key_names[128] = {
                [TERM_KEY_UP] = cstrv("up"),
                [TERM_KEY_DOWN] = cstrv("down"),
                [TERM_KEY_RIGHT] = cstrv("right"),
                [TERM_KEY_LEFT] = cstrv("left")
            };
            
            termkey_e k = keys[buffer[i + 2]];
            e.value = key_names[k];

            i += 2;
        }
        else {
            e.value = strv((char*)&buffer[i], 1);
        }

        should_quit |= term.app.event(&e, term.app.userdata);
    }

    if (should_quit) {
        term.should_quit = should_quit;
    }
}

void term__update_internal(float dt, void *userdata) {
    term_t *tm = userdata;
    term.dt = dt;

    term__poll_input(tm);

    arena_t *frame_arena = term.swap ? &term.swap_arena : &term.frame_arena;
    term.swap = !term.swap;

    arena_rewind(frame_arena, 0);

    term.app.update(frame_arena, dt, term.app.userdata);
    str_t str = term.app.view(frame_arena, term.app.userdata);

    term_write(strv(str));
}

void term_init(termdesc_t *desc) {
    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    term.arena = arena;
    term.frame_arena = arena_make(ARENA_VIRTUAL, GB(1));
    term.swap_arena = arena_make(ARENA_VIRTUAL, GB(1));
    term.sync_mtx = os_mutex_create();
    term.output = TERM_DEFAULT_OUTPUT;
    term.last_lines_count = 1;
    
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

void term_cleanup() {
    if (term.fullscreen) {
        TERM_CLEAR_SCREEN();
    }
    TERM_SHOW_CURSOR();
}

void term_update(term_t *term) {
    ticker_tick(&term->ticker);
}

typedef enum {
    SPINNER_LINE,
    SPINNER_DOT,
    SPINNER_MINIDOT,
    SPINNER_JUMP,
    SPINNER_PULSE,
    SPINNER_POINTS,
    SPINNER_GLOBE,
    SPINNER_MOON,
    SPINNER_METER,
    SPINNER_ELLIPSIS,
    
    SPINNER__COUNT,
} spinner_type_e;

typedef struct spinner_t spinner_t;
struct spinner_t {
    strview_t *frames;
    int count;
    int cur;
    float timer;
    float passed;
};

typedef struct spinner_data_t spinner_data_t;
struct spinner_data_t {
    strview_t frames[16];
    int count;
    float time;
};

spinner_data_t spinner__data[SPINNER__COUNT] = {0};

spinner_t spinner_init(spinner_type_e type) {

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

typedef struct state_t state_t;
struct state_t {
    spinner_t spinner;
    spinner_type_e type;
    float timer;
    float passed;
    // float time;
    // float passed;
    // int frame;
    //
    // int cursor;
    //
    // strview_t *choices;
    // bool *selected;
    // int count;
};

bool test_event(termevent_t *e, void *userdata) {
    state_t *self = userdata;

#define IS_KEY(k) strv_equals(e->value, (strview_t)cstrv(k))

    switch (e->type) {
        case TERM_EVENT_KEY:
            if (!e->is_key_down) {
                break;
            }

            if (IS_KEY("q")) {
                return true;
            }
            //
            // else if (IS_KEY("j") || IS_KEY("down")) {
            //     self->cursor++;
            //     if (self->cursor >= self->count) {
            //         self->cursor = self->count - 1;
            //     }
            // }
            // else if (IS_KEY("k") || IS_KEY("up")) {
            //     self->cursor--;
            //     if (self->cursor < 0) {
            //         self->cursor = 0;
            //     }
            // }
            // else if(IS_KEY(" ") || IS_KEY("l") || IS_KEY("right")) {
            //     self->selected[self->cursor] = !self->selected[self->cursor];
            // }
            break;

        default: break;
    }

    return false;
}

void test_update(arena_t *arena, float dt, void *userdata) {
    state_t *self = userdata;

    spinner_type_e old = self->type;

    self->passed += dt;
    while (self->passed >= self->timer) {
        self->passed -= self->timer;
        self->type = (self->type + 1) % SPINNER__COUNT;
    }

    if (old != self->type) {
        self->spinner = spinner_init(self->type);
    }

    spinner_update(&self->spinner, dt);
}

str_t test_view(arena_t *arena, void *userdata) {
    state_t *self = userdata;

    outstream_t out = ostr_init(arena);

    ostr_print(&out, "<magenta>%v </>waiting forever <blue>📁 <green>📄</>", self->spinner.frames[self->spinner.cur]);

#if 0
    ostr_puts(&out, strv("what should we buy at the market?\n\n"));

    for (int i = 0; i < self->count; ++i) {
        bool selected = self->selected[i];

        const char *cursor = " ";
        if (self->cursor == i) {
            cursor = ">";
        }

        const char *checked = " ";
        if (selected) {
            checked = "<green>X</>";
        }

        ostr_print(&out, "%s [%s] ", cursor, checked);

        if (selected) {
            ostr_print(&out, "<green>%v</>\n", self->choices[i]);
        }
        else {
            ostr_print(&out, "%v\n", self->choices[i]);
        }
    }

    ostr_puts(&out, strv("\npress q to quit\n"));
#endif

    return ostr_to_str(&out);
}

int main() {
    colla_init(COLLA_ALL);

    oshandle_t input = os_stdin();

    DWORD prev = 0; // 0x01f7
    GetConsoleMode((HANDLE)input.data, &prev);

    SetConsoleMode((HANDLE)input.data, ENABLE_VIRTUAL_TERMINAL_INPUT);

    strview_t choices[] = {
        cstrv("Buy carrots"),
        cstrv("Buy celery"),
        cstrv("Buy oranges"),
    };

    bool selected[arrlen(choices)] = {0};

    state_t state = {
        .spinner = spinner_init(0),
        .timer = 2,
    };

    term_init(&(termdesc_t) {
        .fps = 10,
        .app = {
            .update = test_update,
            .view = test_view,
            .event = test_event,
            .userdata = &state,
        },
    });

    term_run();

    term_write(term, test_view(&term->swap_arena, &state));

    while (!term->should_quit) {
        term_update(term);
    }

    term_cleanup(term);
    
    SetConsoleMode((HANDLE)input.data, ~ENABLE_VIRTUAL_TERMINAL_INPUT);
}
#endif

typedef struct state_t state_t;
struct state_t {
    spinner_t spinner;
    spinner_type_e type;
    float timer;
    float passed;
    char key[16];
};

bool test_event(termevent_t *e, void *userdata) {
    state_t *self = userdata;

#define IS_KEY(k) strv_equals(strv(e->value), (strview_t)cstrv(k))

    switch (e->type) {
        case TERM_EVENT_KEY:
            if (!e->is_key_down) {
                break;
            }

            info("key: (%v)", e->value);

            // if (e->key == KEY_Q || (e->modifier & MOD_CTRL && e->key == KEY_C))
            // if (e->key == KEY_Q || e->key == (KEY_C | KEY_CTRL))
            // if (e->key == "q" || e->key == "ctrl-q")
            if (IS_KEY("q")) {
                return true;
            }
            break;

        default: break;
    }

    return false;
}

void test_update(arena_t *arena, float dt, void *userdata) {
    state_t *self = userdata;

    spinner_type_e old = self->type;

    self->passed += dt;
    while (self->passed >= self->timer) {
        self->passed -= self->timer;
        self->type = (self->type + 1) % SPINNER__COUNT;
    }

    if (old != self->type) {
        self->spinner = spinner_init(self->type);
    }

    spinner_update(&self->spinner, dt);
}

str_t test_view(arena_t *arena, void *userdata) {
    state_t *self = userdata;

    outstream_t out = ostr_init(arena);

    // ostr_print(&out, "<magenta>%v </>waiting forever <blue>📁 <green>📄</>", self->spinner.frames[self->spinner.cur]);

    return ostr_to_str(&out);
}

int main() {
    colla_init(COLLA_ALL);

    state_t state = {
        .timer = 2.f,
        .type = SPINNER_LINE,
        .spinner = spinner_init(SPINNER_LINE),
    };

    term_init(&(termdesc_t){
        .app = {
            .userdata = &state,
            .view = test_view,
            .update = test_update,
            .event = test_event
        }
    });

    term_run();
}
