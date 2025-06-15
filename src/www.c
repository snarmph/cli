#include "colla/build.c"

void print_html(arena_t scratch, htmltag_t *root) {
    if (!strv_is_empty(root->content)) {
        pretty_print(scratch, "<blue>\\<%v></> %v\n", root->key, root->content);
    }

    for_each (c, root->children) {
        print_html(scratch, c);
    }
}

void add_tag(outstream_t *out, htmltag_t *tag, int indent) {
    bool has_key = !str_is_empty(tag->key);

    if (has_key) {
        str_lower(&tag->key);
        for (int i = 0; i < indent; ++i) ostr_putc(out, ' ');
        ostr_print(out, "<%v>\n", tag->key);
    }

    for (int i = 0; i < indent; ++i) ostr_putc(out, ' ');
    ostr_puts(out, tag->content);

    for_each (c, tag->children) {
        add_tag(out, c, indent + 4);
    }

    if (has_key) {
        for (int i = 0; i < indent; ++i) ostr_putc(out, ' ');
        ostr_print(out, "</%v>", tag->key);
    }
}

void make_html(arena_t scratch, htmltag_t *root) {
    outstream_t out = ostr_init(&scratch);
    add_tag(&out, root, 0);
    os_file_write_all_str(strv("debug/out-test.html"), ostr_as_view(&out));
}

int main(int argc, char **argv) {
    colla_init(COLLA_ALL);

    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    strview_t webpage = strv("https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html");
    // https://en.wikipedia.org/wiki/Regular_expression
    // http://savannah.nongnu.org/
    
    http_res_t res = http_get(&arena, webpage);
    
    html_t doc = html_parse_str(&arena, res.body);
   
    htmltag_t *body = html_get_tag(doc.root, strv("body"), true);

    // print_html(arena, body);
    make_html(arena, doc.root->children);

    info("finished");
}
