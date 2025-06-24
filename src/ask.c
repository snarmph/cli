#include "colla/build.c"
#include "common.h"

#include "term.c"

#define ATOMIC_SET(v, x) (InterlockedExchange(&v, (x)))
#define ATOMIC_CHECK(v)  (InterlockedCompareExchange(&v, 1, 1))
#define ATOMIC_INC(v) (InterlockedIncrement(&v))
#define ATOMIC_DEC(v) (InterlockedDecrement(&v))
#define ATOMIC_GET(v) (InterlockedOr(&v, 0))

typedef struct options_t options_t;
struct options_t {
    strview_t question;
    strview_t model;
    strview_t api_key;
    strview_t default_prompt;
    strview_t cache_filename;
};

options_t opt = {0};

struct {
    arena_t scratch;
    arena_t arena;
    outstream_t data;
    str_t prev_chunk;
    oshandle_t mtx;
} ask = {0};

struct {
    arena_t md_arena;
    arena_t *arena;
    // bool finished;
    spinner_t spinner;
    outstream_t markdown;
    strview_t prev_parsed;
    str_t parsed;
    oshandle_t mtx;
    volatile long finished;
    bool finished_printing;
} app = {0};

void print_usage(void) {
    println("usage:");
    println("ask <question>");
}

strview_t get_input(arena_t *arena) {
    TCHAR buffer[1024] = {0};
    DWORD read = 0;
    ReadConsole((HANDLE)os_stdin().data, buffer, arrlen(buffer), &read, NULL);
    str_t input = str_from_tstr(arena, tstr_init(buffer, read));
    return strv_trim(strv(input));
}

void setup_conf(arena_t scratch, strview_t filename) {
    strview_t default_model = strv("google/gemma-3n-e4b-it:free");
    strview_t default_prompt = strv("answer succintly to every question that you're asked. respond in simple markdown that is easy to parse");

    print("what model do you want to use? leave empty for %v\n> ", default_model);
    strview_t model = get_input(&scratch);
    if (strv_is_empty(model)) {
        model = default_model;
    }

    print("what about your openrouter.ai key?\n> ");
    strview_t key = get_input(&scratch);

    print("lastly, do you want to setup a prompt for the model? otherwise a default one will be used\n> ");
    strview_t prompt = get_input(&scratch);
    if (strv_is_empty(prompt)) {
        prompt = default_prompt;
    }

    oshandle_t fp = os_file_open(filename, FILEMODE_WRITE);

    os_file_puts(fp, strv("[ask]\n"));
    os_file_print(scratch, fp, "model = %v\n", model);
    os_file_print(scratch, fp, "key = %v\n", key);
    os_file_print(scratch, fp, "prompt = %v\n\n", prompt);

    os_file_close(fp);
}

void load_options(arena_t *arena, int argc, char **argv) {
    if (argc == 1) {
        print_usage();
        os_abort(1);
    }

    str_t conf_fname = STR_EMPTY;
    ini_t conf = common_get_config(arena, &conf_fname);

    if (!ini_is_valid(&conf)) {
        warn("no configuration file was found, let's set it up");
        setup_conf(*arena, strv(conf_fname));
        info("saved configuration to %v", conf_fname);
    }

    initable_t *ini_ask = ini_get_table(&conf, strv("ask"));
    if (!ini_ask) {
        warn("a configuration file was found at %v, but there is no [ask] category", conf_fname);
        os_abort(1);
    }
    
    inivalue_t *model  = ini_get(ini_ask, strv("model"));
    inivalue_t *key    = ini_get(ini_ask, strv("key"));
    inivalue_t *prompt = ini_get(ini_ask, strv("prompt"));

    if (!model) {
        warn("invalid configuration found in %v, model is missing", conf_fname);
        os_abort(1);
    }

    if (!key) {
        warn("invalid configuration found in %v, key is missing", conf_fname);
        os_abort(1);
    }

    opt.model = model->value;
    opt.api_key = key->value;

    // some models don't support this, leave the option to not use it
    if (prompt) opt.default_prompt = prompt->value;

    outstream_t q = ostr_init(arena);
    for (int i = 1; i < argc; ++i) {
        ostr_puts(&q, strv(argv[i]));
        ostr_putc(&q, ' ');
    }

    opt.question = strv_trim(ostr_as_view(&q));
}

typedef enum {
    MD_STATE_NONE,
    MD_STATE_HEADING,
    MD_STATE_LIST,
    MD_STATE_BOLD,
    MD_STATE_CODEHEADER,
    MD_STATE_CODE,
    MD_STATE_CODEINLINE,
    MD_STATE_HOR_LINE,
    MD_STATE_LINK,
    MD_STATE__COUNT,
} md_state_e;

typedef struct {
    os_log_colour_e foreground;
    os_log_colour_e background;
} md_colour_t;

md_colour_t colours[MD_STATE__COUNT] = {
    [MD_STATE_NONE]       = { LOG_COL_RESET, LOG_COL_RESET  },
    [MD_STATE_HEADING]    = { LOG_COL_GREEN, LOG_COL_RESET  },
    [MD_STATE_LIST]       = { LOG_COL_MAGENTA, LOG_COL_RESET  },
    [MD_STATE_BOLD]       = { LOG_COL_RED, LOG_COL_RESET  },
    [MD_STATE_CODEHEADER] = { LOG_COL_YELLOW, LOG_COL_RESET },
    [MD_STATE_CODE]       = { LOG_COL_RESET, LOG_COL_RESET },
    [MD_STATE_CODEINLINE] = { LOG_COL_BLUE, LOG_COL_RESET },
    [MD_STATE_HOR_LINE]   = { LOG_COL_DARK_GREY, LOG_COL_RESET },
    [MD_STATE_LINK]       = { LOG_COL_GREEN, LOG_COL_RESET },
};

#define MAX_STATE_STACK 128

typedef struct md_t md_t;
struct md_t {
    md_state_e last_printed;
    md_state_e states[MAX_STATE_STACK];
    int count;

    bool was_code_newline;
    bool was_code;
    int code_stack;
    bool was_newline;
};

#define head() md.states[md.count - 1]
#define push(new_state) do { \
        md_state_e __head = md.states[md.count - 1]; \
        if (__head != (new_state) && __head != MD_STATE_CODE) { \
            md.states[md.count++] = (new_state); \
        } \
    } while (0)
#define pop() if(md.count > 1) md.count--

#define print_str(v) do { md_try_update_colour(&md, &out); ostr_puts(&out, v); } while(0)
#define print_char(c) do { md_try_update_colour(&md, &out); ostr_putc(&out, c); } while(0)

void md_try_update_colour(md_t *ctx, outstream_t *out) {
    if (
        (ctx->count && ctx->last_printed != ctx->states[ctx->count - 1]) ||
        (!ctx->count && ctx->last_printed != MD_STATE_NONE)
    ) {
        md_colour_t col = colours[ctx->states[ctx->count - 1]];
        ostr_print(out, "</><%v>", pretty_log_to_colour(col.foreground));
        ctx->last_printed = ctx->states[ctx->count - 1];
    }
}

str_t md_parse_chunk(arena_t *arena, strview_t chunk) {
    outstream_t out = ostr_init(arena);

    md_t md = {0};

    instream_t in = istr_init(chunk);
    bool is_newline = false;

    while (!istr_is_finished(&in)) {
        char peek = istr_peek(&in);
        bool is_in_code = md.code_stack > 0;

        if (md.was_code_newline && peek != '`') {
            print_str(strv("│ "));
        }
        md.was_code_newline = false;

        if (md.was_code) {
            md.was_code = false;
            if (peek == '\\' && istr_peek_next(&in) == 'n') {
                if (md.code_stack > 2) {
                    print_str(strv("│"));
                    for (int i = 0; i < md.code_stack; ++i) {
                        print_char(' ');
                    }
                    print_str(strv("```"));
                }
                else {
                    print_str(strv("╰───"));
                    pop();
                }
                md.code_stack--;
            } 
            else {
                if (md.code_stack > 1) {
                    print_str(strv("│"));
                    for (int i = 0; i < md.code_stack; ++i) {
                        print_char(' ');
                    }
                    print_str(strv("```"));
                }
                else {
                    print_str(strv("╭─── "));
                    push(MD_STATE_CODEHEADER);
                }
                md.code_stack++;
            }
            goto skip_print;
        }

        switch (peek) {
            case '#':
                if (!is_in_code && md.was_newline) {
                    push(MD_STATE_HEADING);
                    
                    while (istr_peek(&in) == '#') istr_skip(&in, 1);
                    
                    print_char('>');
                    print_char(' ');
                    istr_skip(&in, 1);
                    goto skip_print;
                }
                break;
            
            case '-':    
                if (is_in_code) break;

                if (md.was_newline && istr_peek_next(&in) != '-') {
                    push(MD_STATE_LIST);
                }
                break;

            case '>':
                if (is_in_code) break;

                if (md.was_newline) {
                    print_str(strv("░"));
                    istr_skip(&in, 1);
                    goto skip_print;
                }
                break;

            case '`':
            {
                int count = 0;
                while (istr_peek(&in) == '`') {
                    istr_skip(&in, 1);
                    count++;
                }

                if (count == 3) {
                    bool is_closing = istr_peek(&in) == '\\' && istr_peek_next(&in) == 'n';
                    if (istr_peek_next(&in) == '\0') {
                        md.was_code = true;
                        goto skip_print;
                    }

                    if (is_closing) {
                        if (md.code_stack > 1) {
                            print_str(strv("│"));
                            for (int i = 0; i < md.code_stack; ++i) {
                                print_char(' ');
                            }
                            print_str(strv("```"));
                        }
                        else {
                            print_str(strv("╰───"));
                            pop();
                        }
                        md.code_stack--;
                    }
                    else {
                        if (md.code_stack > 0) {
                            print_str(strv("│"));
                            for (int i = 0; i < md.code_stack; ++i) {
                                print_char(' ');
                            }
                            print_str(strv("```"));
                        }
                        else {
                            print_str(strv("╭─── "));
                            push(MD_STATE_CODEHEADER);
                        }
                        md.code_stack++;
                    }

                    goto skip_print;
                }

                if  (count == 1) {
                    if (head() == MD_STATE_CODEINLINE) {
                        print_char('`');
                        pop();
                        md.code_stack--;
                    }
                    else {
                        md.code_stack++;
                        push(MD_STATE_CODEINLINE);
                        print_char('`');
                    }
                    goto skip_print;
                }

                break;
            }

            case '\\':
                switch (istr_peek_next(&in)) {
                    case 'n':
                        is_newline = true;

                        istr_skip(&in, 2);
                        if (head() == MD_STATE_CODEHEADER) {
                            pop();
                            push(MD_STATE_CODE);
                        }
                        else if (md.was_newline && head() != MD_STATE_CODE) {
                            pop();
                        }
                        else if (head() == MD_STATE_LIST || head() == MD_STATE_HEADING) {
                            pop();
                        }                       

                        print_char('\n');

                        if (head() == MD_STATE_CODE) {
                            md.was_code_newline = true;
                        }

                        goto skip_print;

                    default:
                        istr_skip(&in, 1);
                        print_char(istr_get(&in));
                        goto skip_print;
                }
                break;

            case '*':
            {
                if (is_in_code) break;

                int count = 0;
                while (istr_peek(&in) == '*') {
                    istr_skip(&in, 1);
                    count++;
                }

                char bold_str[] = "**********";

                if (count == 1 && md.was_newline) {
                    push(MD_STATE_LIST);
                    colla_assert(count < arrlen(bold_str)-1);
                    print_str(strv(bold_str, count));
                }
                else if (head() == MD_STATE_BOLD) {
                    pop();
                }
                else {
                    push(MD_STATE_BOLD);
                }
                goto skip_print;
            }
            
            default:
            {
                if (is_in_code) break;

                if (!char_is_num(peek) || !md.was_newline) {
                    break;
                }

                strview_t num = istr_get_view(&in, '.');
                bool is_all_nums = true;
                for (usize i = 0; i < num.len; ++i) {
                    if (!char_is_num(num.buf[i])) {
                        is_all_nums = false;
                        break;
                    }
                }

                if (!is_all_nums) {
                    istr_rewind_n(&in, num.len);
                }
                else {
                    push(MD_STATE_LIST);
                    print_str(num);
                    goto skip_print;
                }
            }
        }

        if (md.was_newline && char_is_space(peek)) {
            is_newline = true;
        }

        print_char(istr_get(&in));

skip_print:
        md.was_newline = is_newline;
        is_newline = false;
    }

    ostr_puts(&out, strv("</>"));

    return ostr_to_str(&out);
}

void ask_grab_chunk(strview_t chunk, void *userdata) {
    COLLA_UNUSED(userdata);
    arena_t scratch = ask.scratch;

    if (!str_is_empty(ask.prev_chunk)) {
        str_t full = str_fmt(&scratch, "%v%v", ask.prev_chunk, chunk);
        chunk = strv(full);
        ask.prev_chunk = STR_EMPTY;
    }
    
    instream_t in = istr_init(chunk);

    while (!istr_is_finished(&in)) {
        strview_t line = istr_get_line(&in);

        if (!strv_starts_with_view(line, strv("data: "))) {
            continue;
        }

        // incomplete chunk
        if (istr_is_finished(&in)) {
            if (!str_is_empty(ask.prev_chunk)) {
                err("chunk should be empty");
            }
            ask.prev_chunk = str(&ask.scratch, line);
            return;
        }

        // remove "data: "
        line = strv_remove_prefix(line, 6);

        if (strv_equals(line, strv("[DONE]"))) {
            break;
        }

        json_t *json = json_parse_str(&scratch, line, JSON_DEFAULT);
        if (!json) fatal("1 failed to parse json: %v", line);
        json_t *choices = json_get(json, strv("choices"));
        if (!choices) fatal("2 failed to get choices: %v", line);
        json_t *item = choices->array;
        if (!item)  fatal("3 failed to get item: %v", line);

        json_t *delta = json_get(item, strv("delta"));
        if (!delta) fatal("4 failed to get delta: %v", line);
        json_t *content = json_get(delta, strv("content"));
        if (!content) fatal("5 failed to get content: %v", line);

        if (strv_is_empty(content->string)) {
            continue;
        }

        os_mutex_lock(ask.mtx);
            ostr_puts(&ask.data, content->string);
        os_mutex_unlock(ask.mtx);

    }
}

int request_thread(u64 id, void *udata) {
    COLLA_UNUSED(id); COLLA_UNUSED(udata);

    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    str_t auth_header = str_fmt(&arena, "Bearer %v", opt.api_key);

    str_t body = STR_EMPTY;

    if (strv_is_empty(opt.default_prompt)) {
        body = str_fmt(&arena, 
            "{"
                "\"model\": \"%v\","
                "\"stream\": true,"
                "\"messages\": ["
                    "{"
                        "\"role\": \"user\","
                        "\"content\": \"%v\","
                    "}"
                "]"
            "}",
            opt.model,
            opt.question
        );
    }
    else {
        body = str_fmt(&arena, 
            "{"
                "\"model\": \"%v\","
                "\"stream\": true,"
                "\"messages\": ["
                    "{"
                        "\"role\": \"system\","
                        "\"content\": \"%v\","
                    "},"
                    "{"
                        "\"role\": \"user\","
                        "\"content\": \"%v\","
                    "}"
                "]"
            "}",
            opt.model,
            opt.default_prompt,
            opt.question
        );
    }

    http_request_desc_t req = {
        .arena = &arena,
        .url = strv("https://openrouter.ai/api/v1/chat/completions"),
        .request_type = HTTP_POST,
        .headers = (http_header_t []) {
            { strv("Authorization"), strv(auth_header), },
            { strv("Content-Type"),  strv("application/json"), } ,
        },
        .header_count = 2,
        .body = strv(body), 
    };

#define TEST 1
#if TEST
    str_t content = os_file_read_all_str(&arena, strv("debug/output.md"));

    for (usize i = 0; i < content.len; i += 4) {
        strview_t chunk = str_sub(content, i, i + 4);
        os_mutex_lock(ask.mtx);
            ostr_puts(&ask.data, chunk);
        os_mutex_unlock(ask.mtx);
        Sleep(200);
    }

#else
    http_res_t res = http_request_cb(&req, ask_grab_chunk, NULL);

    if (res.status_code != 200) {
        err("request failed:");
        json_t *json = json_parse_str(&arena, res.body, JSON_DEFAULT);
        json_pretty_print(json, NULL);
    }
    
    os_file_write_all_str(strv("debug/output.json"), strv(http_res_to_str(&arena, &res)));
#endif

    ATOMIC_SET(app.finished, true);

    return 0;
}

bool app_update(arena_t *arena, float dt, void *udata) {
    COLLA_UNUSED(arena); COLLA_UNUSED(udata);
    spinner_update(&app.spinner, dt);
    return app.finished_printing;
}

void app_event(termevent_t *e, void *udata) {
    COLLA_UNUSED(udata);
    if (e->type == TERM_EVENT_KEY &&
        strv_equals(strv(e->value), strv("q"))
       ) {
        app.finished_printing = true;
    }
}

str_t app_view(arena_t *arena, void *udata) {
    COLLA_UNUSED(udata);
    outstream_t out = ostr_init(arena);

#if 0
    if (os_mutex_try_lock(ask.mtx)) {
        app.prev_parsed = app.parsed;
        os_mutex_unlock(app.mtx);
        if (ATOMIC_CHECK(app.finished)) {
            app.finished_printing = true;
        }
    }

    ostr_puts(&out, app.prev_parsed);

    if (!ATOMIC_CHECK(app.finished)) {
        strview_t nl = strv("\n");
        if (strv_is_empty(app.prev_parsed)) {
            nl = strv("");
        }

        ostr_print(&out, "%v<magenta>%v</> getting response", nl, app.spinner.frames[app.spinner.cur]);
    }
#endif

    if (os_mutex_try_lock(ask.mtx)) {
        strview_t chunk = ostr_as_view(&ask.data);
        os_mutex_unlock(ask.mtx);

        if (!strv_equals(chunk, app.prev_parsed)) {
            app.prev_parsed = chunk;
            arena_rewind(&app.md_arena, 0);
            app.parsed = md_parse_chunk(&app.md_arena, chunk);
        }
    }

    //arena_t scratch = *app.arena;

    //str_t parsed = md_parse_chunk(&scratch, app.prev_parsed);
    ostr_puts(&out, strv(app.parsed));

    return ostr_to_str(&out);
}

int main(int argc, char **argv) {
    colla_init(COLLA_ALL);
    
    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    load_options(&arena, argc, argv);

    print("hello\nworld\nhello\nworld\n");
    print("hello\nworld\nhello\nworld\n");
    print("hello\nworld\nhello\nworld\n");
    print("hello\nworld\nhello\nworld\n");
    print("hello\nworld\nhello\nworld\n");
    print("hello\nworld\nhello\nworld\n");
    print("hello\nworld\nhello\nworld\n");
    print("hello\nworld\nhello\nworld\n");

    app.md_arena = arena_make(ARENA_VIRTUAL, GB(1));
    app.spinner = spinner_init(SPINNER_DOT);
    app.mtx = os_mutex_create();
    app.arena = &arena;
    app.markdown = ostr_init(app.arena);

    ask.scratch = arena_make(ARENA_VIRTUAL, GB(1));
    ask.arena = arena_make(ARENA_VIRTUAL, GB(1));
    ask.data = ostr_init(&ask.arena);
    ask.mtx = os_mutex_create();

    oshandle_t req_thread = os_thread_launch(request_thread, NULL);
    os_thread_detach(req_thread);

    term_init(&(termdesc_t){
        .app = {
            .event = app_event,
            .update = app_update,
            .view = app_view,
        },
        // .fullscreen = true,
    });

    term_run();
    
    os_log_set_colour_bg(LOG_COL_WHITE, LOG_COL_BLACK);
}

