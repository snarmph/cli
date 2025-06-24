#include "colla/build.c"

typedef struct options_t options_t;
struct options_t {
    http_method_e method;
    strview_t url;
    http_header_t *headers;
    str_t body;

    bool body_only;
    bool clean;
};

options_t opt = {0};

void print_usage(void) {
    print("usage: http [flags] [method] <url> [key=value] [body]\n");
    print("options:");
    print("\t-b  --body-only  only print the body\n");
    print("\t-c  --clean      don't do any pretty printing\n");
    print("\t-p  --pipe       same as -b -c, useful for piping\n");
}

http_header_t *parse_header(arena_t *arena, strview_t arg) {
    usize index = strv_find(arg, '=', 0);

    http_header_t *h = alloc(arena, http_header_t);
    h->key = strv_sub(arg, 0, index);
    h->value = strv_sub(arg, index + 1, SIZE_MAX);

    return h;
}

void parse_options(arena_t *arena, int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        os_abort(1);
    }

#define IS_OPT(short, long) (strv_equals(arg, strv(short)) || strv_equals(arg, strv(long)))

    // parse flags first
    int cur = 1;
    for (; cur < argc; ++cur) {
        strview_t arg = strv(argv[cur]);
        if (arg.buf[0] != '-') break;
        
        if (IS_OPT("-b", "--body-only")) {
            opt.body_only = true;
        }
        else if (IS_OPT("-c", "--clean")) {
            opt.clean = true;
        }
        else if (IS_OPT("-p", "--pipe")) {
            opt.body_only = true;
            opt.clean = true;
        }
    }

    strview_t method_or_url = strv(argv[cur]);

    strview_t str_to_method[HTTP_METHOD__COUNT] = {
        cstrv("GET"),
        cstrv("POST"),
        cstrv("HEAD"),
        cstrv("PUT"),
        cstrv("DELETE"),
    };

    bool found = false;

    for (usize i = 0; i < arrlen(str_to_method); ++i) {
        if (strv_equals(method_or_url, str_to_method[i])) {
            found = true;
            opt.method = i;
            break;
        }
    }

    opt.url = found ? strv(argv[cur + 1]) : method_or_url;

    cur += 1 + found;

    outstream_t body = {0}; 
    bool is_body = false;

    for (int i = cur; i < argc; ++i) {
        strview_t arg = strv(argv[i]);
        if (!is_body && strv_contains(arg, '=')) {
            http_header_t *newheader = parse_header(arena, arg);
            list_push(opt.headers, newheader);
        }
        else {
            if (body.arena == NULL) {
                body = ostr_init(arena);
                is_body = true;
            }
            else {
                ostr_putc(&body, ' ');
            }
            ostr_puts(&body, arg);
        }
    }

    opt.body = ostr_to_str(&body);
}

int main(int argc, char **argv) {
    colla_init(COLLA_ALL);

    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    parse_options(&arena, argc, argv);

    http_res_t response = http_request(&(http_request_desc_t){
        .arena = &arena,
        .url   = opt.url,
        .request_type = opt.method,
        .headers = opt.headers,
        .body = strv(opt.body),
    });

    if (response.status_code == 0) {
        return 1;
    }

    if (!opt.body_only) {
        if (opt.clean) {
            print("HTTP/%d.%d, %d %s\n", 
                    response.version.major, response.version.minor,
                    response.status_code, http_get_status_string(response.status_code));

            for_each (h, response.headers) {
                print("%v: %v\n", h->key, h->value);
            }

            print("\n");
        }
        else {
            pretty_print(
                arena, 
                "<grey>HTTP/%d.%d <green>%d %s\n",
                response.version.major, response.version.minor,
                response.status_code, http_get_status_string(response.status_code)
            );

            for_each (h, response.headers) {
                pretty_print(arena, "<blue>%v: </>%v\n", h->key, h->value);
            }

            os_log_set_colour(LOG_COL_RESET);

            print("\n");
        }
    }

    strview_t content_type = http_get_header(response.headers, strv("Content-Type"));
    
    bool is_json = strv_contains_view(content_type, strv("json"));

    if (!opt.clean && is_json) {
        if (!strv_starts_with(response.body, '{')) {
            str_t full = str_fmt(&arena, "{ \"data\": %v }", response.body);
            response.body = strv(full);
        }

        json_t *json = json_parse_str(&arena, response.body, JSON_DEFAULT);

        json_pretty_print(json, &(json_pretty_opts_t){0}); 
    }
    else {
        println("%v", response.body);
    }
}
