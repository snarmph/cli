#include "colla/build.c"
#include "colla/highlight.c"

typedef enum {
    HG_NONE,
    HG_CLIKE,
    HG_JSON,
    HG_INI,
    HG_XML,

    HG__COUNT,
} highlighters_e;

typedef struct lang_t lang_t;
struct lang_t {
    highlighters_e highlighter;
    strview_t ext;
};

lang_t recognised_langs[] = {
    { HG_CLIKE, cstrv(".h") },
    { HG_CLIKE, cstrv(".hh") },
    { HG_CLIKE, cstrv(".hpp") },
    { HG_CLIKE, cstrv(".c") },
    { HG_CLIKE, cstrv(".cc") },
    { HG_CLIKE, cstrv(".cpp") },
    { HG_CLIKE, cstrv(".c++") },
    { HG_JSON,  cstrv(".json") },
    { HG_INI,   cstrv(".ini") },
    { HG_INI,   cstrv(".toml") },
    { HG_XML,   cstrv(".xml") },
};


void pretty_print_json(strview_t data);
void pretty_print_ini(strview_t data);
void pretty_print_xml(strview_t data);

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        println("usage:");
        println("\tcat [options] <file>");
        println("\toptions:");
        println("\t\t-p    print plain, without any highlighting");
        return 1;
    }

    bool plain = false;
    strview_t filename = STRV_EMPTY; 

    for (int i = 1; i < argc; ++i) {
        strview_t arg = strv(argv[i]);
        if (strv_equals(arg, strv("-p"))) {
            plain = true;
        }
        else {
            filename = arg;
        }   
    }

    os_init();

    if (!os_file_exists(filename)) {
        err("no file by the name \"%v\"", filename);
        return 1;
    }

    strview_t ext;
    os_file_split_path(filename, NULL, NULL, &ext);

    highlighters_e filetype = HG_NONE;
    
    if (!plain) {
        for (usize i = 0; i < arrlen(recognised_langs); ++i) {
            if (strv_equals(recognised_langs[i].ext, ext)) {
                filetype = recognised_langs[i].highlighter;
                break;
            }
        }
    }

    oshandle_t out = os_stdout();

    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    oshandle_t fp = os_file_open(filename, FILEMODE_READ);

    const usize maxsize = MB(10);

    usize filesize = os_file_size(fp);

    // the file is too long to try to pretty print it, just print it directly
    if (filesize > maxsize) {
        u8 *buffer = alloc(&arena, u8, maxsize);
        while (!os_file_is_finished(fp)) {
            usize read = os_file_read(fp, buffer, maxsize);
            os_file_write(out, buffer, read);
        }
    }
    else {
        str_t data = os_file_read_all_str_fp(&arena, fp);

        switch (filetype) {
            case HG_NONE:
                os_file_write_all_str_fp(out, strv(data));
                break;
            case HG_CLIKE:
                break;
            case HG_JSON:
                pretty_print_json(strv(data));
                break;
            case HG_INI:
                pretty_print_ini(strv(data));
                break;
            case HG_XML:
                break;
        }
    }

#if 0
#if 0
    hl_ctx_t *highlighter = NULL;

    if (is_clike) {
        arena = arena_make(ARENA_VIRTUAL, GB(1));
        highlighter = hl_init(&arena, &(hl_config_t){
            .colors = {
                [HL_COLOR_NORMAL] = cstrv("<white>"),
                [HL_COLOR_PREPROC] = cstrv("<red>"),
                [HL_COLOR_TYPES] = cstrv("<red>"),
                [HL_COLOR_CUSTOM_TYPES] = cstrv("<red>"),
                [HL_COLOR_KEYWORDS] = cstrv("<magenta>"),
                [HL_COLOR_NUMBER] = cstrv("<red>"),
                [HL_COLOR_STRING] = cstrv("<green>"),
                [HL_COLOR_COMMENT] = cstrv("<grey>"),
                [HL_COLOR_FUNC] = cstrv("<blue>"),
                [HL_COLOR_SYMBOL] = cstrv("<yellow>"),
                [HL_COLOR_MACRO] = cstrv("<magenta>"),
            },
        });
    }
#endif

    oshandle_t fp = os_file_open(filename, FILEMODE_READ);
    if (!os_handle_valid(fp)) {
        err("error opening file %x: %v", filename, os_get_error_string(os_get_last_error()));
        return 1;
    }

    char buffer[KB(5)] = {0};

    oshandle_t out = os_stdout();
    while (!os_file_is_finished(fp)) {
        usize read = os_file_read(fp, buffer, arrlen(buffer));
        
        strview_t text = strv(buffer, read);
#if 0
        if (highlighter) {
            arena_t scratch = arena;
            str_t highlighted = hl_highlight(&scratch, highlighter, text);
            text = strv(highlighted);
            pretty_print(scratch, "%v", text);
        }
#endif
        if (filetype) {
            
        }
        else {
            os_file_write(out, buffer, read);
        }
    }
#endif
}

void pretty_print_json(strview_t data) {
    instream_t in = istr_init(data);

#if 0
if {:
    print {

#endif

    while (!istr_is_finished(&in)) {
        char c = istr_peek(&in);

        if (char_is_space(c)) {
            print("%c", c);
            istr_skip(&in, 1);
            continue;
        }

        if (char_is_num(c) || c == '.') {
            os_log_set_colour(LOG_COL_RED);
            print("%c", c);
            istr_skip(&in, 1);
            continue;
        }

        if (c == 't') {
            strview_t str = istr_get_view_len(&in, 4);
            os_log_set_colour(LOG_COL_MAGENTA);
            print("%v", str);
            continue;
        }

        if (c == 'f') {
            strview_t str = istr_get_view_len(&in, 5);
            os_log_set_colour(LOG_COL_MAGENTA);
            print("%v", str);
            continue;
        }

        if (c == 'n') {
            strview_t str = istr_get_view_len(&in, 4);
            os_log_set_colour(LOG_COL_GREY);
            print("%v", str);
            continue;
        }

        if (strv_contains(strv(",:"), c)) {
            os_log_set_colour(LOG_COL_WHITE);
            print("%c", c);
            istr_skip(&in, 1);
            continue;
        }

        if (strv_contains(strv("{}[]"), c)) {
            os_log_set_colour(LOG_COL_GREY);
            print("%c", c);
            istr_skip(&in, 1);
            continue;
        }

        if (c == '"') {
            istr_skip(&in, 1);
            strview_t str = istr_get_view(&in, '"');
            istr_skip(&in, 1);

            os_log_set_colour(LOG_COL_YELLOW);
            print("\"%v\"", str);
        }
    }

    os_log_set_colour(LOG_COL_RESET);
}

void pretty_print_ini(strview_t data) {
    instream_t in = istr_init(data);

    while (!istr_is_finished(&in)) {
        char c = istr_peek(&in);

        if (char_is_space(c)) {
            print("%c", c);
            istr_skip(&in, 1);
            continue;
        }

        if (c == '[') {
            os_log_set_colour(LOG_COL_RED);
            istr_skip(&in, 1);
            strview_t cat = istr_get_view(&in, ']');
            istr_skip(&in, 1);
            print("[%v]", cat);
            os_log_set_colour(LOG_COL_RESET);
            continue;
        }

        if (c == '#' || c == ';') {
            os_log_set_colour(LOG_COL_GREY);
            strview_t com = istr_get_line(&in);
            print("%v\n", com);
            os_log_set_colour(LOG_COL_RESET);
            continue;
        }

        strview_t key = istr_get_view(&in, '=');
        istr_skip(&in, 1);
        strview_t val = istr_get_view_either(&in, strv("\n\r#;")); 

        os_log_set_colour(LOG_COL_YELLOW);
        print("%v", key);
        os_log_set_colour(LOG_COL_WHITE);
        print("=");
        os_log_set_colour(LOG_COL_GREEN);
        print("%v", val);
        os_log_set_colour(LOG_COL_RESET);
    }
}

void pretty_print_xml(strview_t data) {
    // TODO
    COLLA_UNUSED(data);
    print("%v", data);
}



