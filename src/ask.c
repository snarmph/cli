#include "colla/build.c"

typedef struct options_t options_t;
struct options_t {
    strview_t question;
    strview_t model;
    strview_t api_key;
    strview_t default_prompt;
    strview_t cache_filename;
};

options_t opt = {0};

str_t get_local_folder(arena_t *arena) {
    u8 tmpbuf[KB(5)];
    arena_t scratch = arena_make(ARENA_STATIC, sizeof(tmpbuf), tmpbuf);

    TCHAR tpath[512] = {0};

    DWORD len = GetModuleFileName(NULL, tpath, arrlen(tpath));

    str_t path = str_from_tstr(&scratch, tstr_init(tpath, len));
    
    strview_t dir;
    os_file_split_path(strv(path), &dir, NULL, NULL);

    return str(arena, dir);
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

    // while (true) {
    //     os_wait_t res = os_wait_on_handles(&input, 1, true, OS_WAIT_INFINITE);
    //     info("res: %d", res.result);
    //     str_t data = os_file_read_all_str_fp(&scratch, input);
    //     info("(%v)", data);
    //     break;
    // }
}

void load_options(arena_t *arena, int argc, char **argv) {
    str_t local_dir = get_local_folder(arena);

    str_t conf_fname = str_fmt(arena, "%v/conf.ini", local_dir);

    if (!os_file_exists(strv(conf_fname))) {
        warn("no configuration file was found, let's set it up");
        setup_conf(*arena, strv(conf_fname));
        info("saved configuration to %v", conf_fname);
    }

    ini_t conf = ini_parse(arena, strv(conf_fname), NULL);

    initable_t *ask = ini_get_table(&conf, strv("ask"));
    if (!ask) {
        warn("a configuration file was found at %v, but there is no [ask] category", conf_fname);
        os_abort(1);
    }
    
    inivalue_t *model  = ini_get(ask, strv("model"));
    inivalue_t *key    = ini_get(ask, strv("key"));
    inivalue_t *prompt = ini_get(ask, strv("prompt"));

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
    [MD_STATE_LINK]       = { LOG_COL_GREEN, LOG_COL_RESET },
};

#define MAX_STATE_STACK 128

typedef struct md_t md_t;
struct md_t {
    arena_t scratch;
    str_t prev_chunk;
    bool was_code_newline;
    md_state_e last_printed;
    md_state_e states[MAX_STATE_STACK];
    int count;
    oshandle_t debug_fp;
};

#define head() ctx->states[ctx->count - 1]
#define push(new_state) do { \
        md_state_e __head = ctx->states[ctx->count - 1]; \
        if (__head != (new_state) && __head != MD_STATE_CODE) { \
            ctx->states[ctx->count++] = (new_state); \
        } \
    } while (0)
#define pop() if(ctx->count > 1) ctx->count--

#define print_str(ctx, v) do { md_try_update_colour(ctx); os_file_puts(os_stdout(), v); } while(0)
#define print_char(ctx, c) do { md_try_update_colour(ctx); os_file_putc(os_stdout(), c); } while(0)

void md_try_update_colour(md_t *ctx) {
    if (
        (ctx->count && ctx->last_printed != ctx->states[ctx->count - 1]) ||
        (!ctx->count && ctx->last_printed != MD_STATE_NONE)
    ) {
        md_colour_t col = colours[ctx->states[ctx->count - 1]];
        os_log_set_colour_bg(col.foreground, col.background);
        ctx->last_printed = ctx->states[ctx->count - 1];
    }
}

void md_print_chunk(strview_t content, md_t *ctx) {
    os_file_puts(ctx->debug_fp, content);

    instream_t in = istr_init(content);
    bool was_newline = true;
    bool is_newline = false;
    //
    // if (ctx->was_code_newline && istr_peek(&in) != '`') {
    //     print_str(ctx, strv("│ "));
    // }
    //
    // ctx->was_code_newline = false;

    while (!istr_is_finished(&in)) {
        char peek = istr_peek(&in);
        bool is_in_code = head() == MD_STATE_CODE || head() == MD_STATE_CODEINLINE;

        if (ctx->was_code_newline && peek != '`') {
            print_str(ctx, strv("| "));
        }
        ctx->was_code_newline = false;

        switch (peek) {
            case '#':
                if (!is_in_code && was_newline) {
                    push(MD_STATE_HEADING);
                    print_char(ctx, '>');
                    istr_skip(&in, 1);
                    goto skip_print;
                }
                break;
            
            case '-':    
                if (is_in_code) break;

                if (was_newline && istr_peek_next(&in) != '-') {
                    push(MD_STATE_LIST);
                }
                break;

            case '>':
                if (is_in_code) break;

                if (was_newline) {
                    print_str(ctx, strv("░"));
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
                    if (head() == MD_STATE_CODE) {
                        print_str(ctx, strv("╰───"));
                        // print_str(ctx, strv("░▒▓"));
                        pop();
                    }
                    else {
                        // print_str(ctx, strv("▓▒░ "));
                        print_str(ctx, strv("╭─── "));
                        push(MD_STATE_CODEHEADER);
                    }
                    goto skip_print;
                }
                if (count == 1) {
                    if (head() == MD_STATE_CODEINLINE) {
                        print_char(ctx, '`');
                        pop();
                    }
                    else {
                        push(MD_STATE_CODEINLINE);
                        print_char(ctx, '`');
                    }
                    goto skip_print;
                }
                break;
            }

            case '\\':
                switch (istr_peek_next(&in)) {
                    case '`':
                        istr_skip(&in, 2);
                        print_char(ctx, '`');
                        goto skip_print;

                    case 'n':
                        is_newline = true;

                        istr_skip(&in, 2);
                        if (head() == MD_STATE_CODEHEADER) {
                            pop();
                            push(MD_STATE_CODE);
                        }
                        else if (was_newline && head() != MD_STATE_CODE) {
                            pop();
                        }
                        else if (head() == MD_STATE_LIST) {
                            pop();
                        }                       

                        print_char(ctx, '\n');

                        if (head() == MD_STATE_CODE) {
                            ctx->was_code_newline = true;
                        }

                        // char next = istr_peek(&in);
                        //
                        // if (head() == MD_STATE_CODE && next != '`') {
                        //     if (next) {
                        //         print_str(ctx, strv("│ "));
                        //     }
                        //     else {
                        //         ctx->was_code_newline = true;
                        //     }
                        // }
                        //
                        goto skip_print;

                    default:
                        istr_skip(&in, 1);
                        print_char(ctx, istr_get(&in));
                        goto skip_print;
                }
                break;

            case '*':
            {
                if (is_in_code) break;

                char prev = istr_prev(&in);
                int count = 0;
                while (istr_peek(&in) == '*') {
                    istr_skip(&in, 1);
                    count++;
                }

                char bold_str[] = "**********";

                if (count == 1 && was_newline) {
                    push(MD_STATE_LIST);
                    assert(count < arrlen(bold_str)-1);
                    print_str(ctx, strv(bold_str, count));
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

                if (!char_is_num(peek) || !was_newline) {
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
                    print_str(ctx, num);
                    goto skip_print;
                }
            }
        }

        if (was_newline && char_is_space(peek)) {
            is_newline = true;
        }

        print_char(ctx, istr_get(&in));

skip_print:
        was_newline = is_newline;
        is_newline = false;
    }
}

void md_eval_chunk(strview_t chunk, void *userdata) {
    md_t *ctx = userdata;
    
    if (!strv_contains(chunk, '\n')) {
        if (!str_is_empty(ctx->prev_chunk)) {
            err("chunk should be empty");
        }
        ctx->prev_chunk = str(&ctx->scratch, chunk);
        return;
    }
    
    usize begin = strv_find_view(chunk, strv("data: "), 0);
    if (begin == STR_NONE) {
        return;
    }

    chunk = strv_sub(chunk, begin, SIZE_MAX);

    arena_t scratch = ctx->scratch;

    if (!str_is_empty(ctx->prev_chunk)) {
        str_t full = str_fmt(&scratch, "%v%v", ctx->prev_chunk, chunk);
        chunk = strv(full);
        ctx->prev_chunk = STR_EMPTY;
    }

    // remove "data: "
    chunk = strv_remove_prefix(chunk, 6);
    
    json_t *json = json_parse_str(&scratch, chunk, JSON_DEFAULT);
    if (!json) return fatal("failed to parse json: %v", chunk);
    json_t *choices = json_get(json, strv("choices"));
    if (!choices) return fatal("failed to get choices: %v", chunk);
    json_t *item = choices->array;
    if (!item) return fatal("failed to get item: %v", chunk);

    json_t *delta = json_get(item, strv("delta"));
    if (!delta) return fatal("failed to get delta: %v", chunk);
    json_t *content = json_get(delta, strv("content"));
    if (!content) return fatal("failed to get content: %v", chunk);

    md_print_chunk(content->string, ctx);
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

    md_t md = {
        .scratch = arena_scratch(&arena, MB(5)),
        .debug_fp = os_file_open(strv("debug/output.md"), FILEMODE_WRITE),
        .count = 1,
    };

    http_res_t res = http_request_cb(&req, md_eval_chunk, &md);

    if (res.status_code != 200) {
        err("request failed:");
        json_t *json = json_parse_str(&arena, res.body, JSON_DEFAULT);
        json_pretty_print(json, NULL);
    }
    
    os_file_write_all_str(strv("debug/output.json"), strv(http_res_to_str(&arena, &res)));

    return 0;
}

void test(arena_t arena) {
    str_t data = os_file_read_all_str(&arena, strv("debug/output.md"));
    md_t md = {
        .scratch = arena_scratch(&arena, MB(5)),
        .count = 1,
    };

    md_print_chunk(strv(data), &md);

    os_log_set_colour_bg(LOG_COL_WHITE, LOG_COL_BLACK);

    os_abort(0);
}

int main(int argc, char **argv) {
    colla_init(COLLA_ALL);
    
    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    load_options(&arena, argc, argv);

    test(arena);

    request_thread(0, NULL);
    
    os_log_set_colour_bg(LOG_COL_WHITE, LOG_COL_BLACK);
}

