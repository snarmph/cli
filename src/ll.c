#include "colla\build.c"

#include "common.h"
#include "icons.h"

typedef struct options_t options_t;
struct options_t {
    bool list_all;
    bool list_dirs;
    bool print_tree;
    strview_t dir;
    icon_style_e style;
};

typedef struct entry_t entry_t;
struct entry_t {
    str_t path;
    dir_type_e type;
    entry_t *next;
    entry_t *prev;
    entry_t *children;
};

options_t opt = {0};

void print_usage(void) {
    println("usage:");
    println("\tll [options] [dir (default: .)]");
    println("\toptions:");
    println("\t\t-h --help      show usage");
    println("\t\t-x --hidden    list hidden folders");
    println("\t\t-d --dirs      only print folders");
    println("\t\t-t --tree      print directory tree");
    println("\t\t-e --emojis    use emojis instead of icons");
    println("\t\t-c --no-icons  don't use icons");
}

void load_from_config(ini_t *conf) {
    initable_t *ll = ini_get_table(conf, strv("ll"));
    if (!ll) return;

    inivalue_t *hidden = ini_get(ll, strv("hidden"));
    inivalue_t *dirs   = ini_get(ll, strv("dirs"));
    inivalue_t *tree   = ini_get(ll, strv("tree"));
    inivalue_t *emojis = ini_get(ll, strv("emojis"));
    inivalue_t *icon   = ini_get(ll, strv("icons"));

    if (hidden) opt.list_all   = ini_as_bool(hidden);
    if (dirs)   opt.list_dirs  = ini_as_bool(dirs);
    if (tree)   opt.print_tree = ini_as_bool(tree);
    if (emojis && ini_as_bool(emojis)) opt.style = ICON_STYLE_EMOJI;
    if (icon   && !ini_as_bool(icon))  opt.style = ICON_STYLE_NONE;
}

void load_options(arena_t scratch, int argc, char **argv) {
#define IS_OPT(short, long) strv_equals(arg, strv(short)) || strv_equals(arg, strv(long))

    opt.style = ICON_STYLE_NERD;

    for (int i = 1; i < argc; ++i ) {
        strview_t arg = strv(argv[i]);
        if (IS_OPT("-h", "--help")) {
            print_usage();
            os_abort(0);
        }
        else if (IS_OPT("-x", "--hidden")) {
            opt.list_all = true;
        }
        else if (IS_OPT("-d", "--dirs")) {
            opt.list_dirs = true;
        }
        else if (IS_OPT("-t", "--tree")) {
            opt.print_tree = true;
        }
        else if(IS_OPT("-e", "--emojis")) {
            opt.style = ICON_STYLE_EMOJI;
        }
        else if (IS_OPT("-c", "--no-icons")) {
            opt.style = ICON_STYLE_NONE;
        }
        else {
            opt.dir = arg;
        }
    }


#undef IS_OPT

    ini_t conf = common_get_config(&scratch, NULL);
    if (ini_is_valid(&conf)) {
        load_from_config(&conf);
    }

    if (strv_ends_with(opt.dir, '/') || strv_ends_with(opt.dir, '\\')) {
        // check if that we're not trying to access a drive (e.g. c:\ or f:\)
        if (opt.dir.len < 2 || opt.dir.buf[1] != ':') {
            opt.dir = strv_remove_suffix(opt.dir, 1);
        }
    }
}

entry_t *add_dir(arena_t *arena, strview_t path) {
    entry_t *head = NULL;
    dir_t *dir = os_dir_open(arena, path);

    dir_foreach (arena, entry, dir) {
        if (!opt.list_all && entry->name.buf[0] == '.') {
            continue;
        }

        if (opt.list_dirs && entry->type == DIRTYPE_FILE) {
            continue;
        }

        entry_t *new_entry = alloc(arena, entry_t);
        new_entry->type = entry->type;
        new_entry->path = entry->name;
 
        if (opt.print_tree && entry->type == DIRTYPE_DIR) {
            str_t fullpath = str_fmt(arena, "%v/%v", path, entry->name);
            new_entry->children = add_dir(arena, strv(fullpath)); 
        }
       
        dlist_push(head, new_entry);
    }

    return head;
}

entry_t *order_entries(entry_t *entries) {
    entry_t *out = NULL;

    entry_t *e = entries;
    while (e) {
        entry_t *next = e->next;

        if (e->type == DIRTYPE_FILE) {
            dlist_pop(entries, e);
            dlist_push(out, e);
        }

        e = next;
    }

    e = entries;
    while (e) {
        entry_t *next = e->next;
        dlist_pop(entries, e);
        dlist_push(out, e);

        entry_t *children = order_entries(e->children);
        e->children = children;

        e = next;
    }

    return out;
}

void print_dir(entry_t *entries, int indent) {
    for_each (e, entries) {
        print("%*s", indent * 4, "");

        if (e->type == DIRTYPE_FILE) {
            // os_log_set_colour(LOG_COL_GREEN);
            strview_t ext;
            os_file_split_path(strv(e->path), NULL, NULL, &ext);
            strview_t icon = ext_to_ico(ext);
            // print("%s %v\n", icon, e->path);
            os_log_set_colour(LOG_COL_GREEN);
            print("%v ", icon);
            os_log_set_colour(LOG_COL_RESET);
            print("%v\n", e->path);
        }
        else {
            os_log_set_colour(LOG_COL_BLUE);
            print("%v %v\n", icons[opt.style][ICON_FOLDER], e->path);

            print_dir(e->children, indent + 1);
        }
    }
    
    os_log_set_colour(LOG_COL_RESET);
}

int main(int argc, char **argv) {
    os_init();

    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    load_options(arena, argc, argv);
    if (strv_is_empty(opt.dir)) {
        opt.dir = strv(".");
    }

    icons_init(opt.style);

    entry_t *entries = add_dir(&arena, opt.dir);
    entries = order_entries(entries);
    print_dir(entries, 0);
}
