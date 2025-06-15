#include "colla/build.c"

void create_intermediate(strview_t path) {
    strview_t dirs;
    os_file_split_path(path, &dirs, NULL, NULL);

    usize index = 0;
    strview_t make_path = { path.buf, 0 };

    while (index != STR_NONE) {
        index = strv_find_either(dirs, strv("\\/"), 0);

        strview_t newdir = strv_sub(dirs, 0, index);
        dirs = strv_remove_prefix(dirs, index != STR_NONE ? index + 1 : 0);

        make_path.len += newdir.len + 1;
        if (os_dir_exists(make_path)) {
            continue;
        }
        
        if (!os_dir_create(make_path)) {
            fatal("couldn't create folder (%v): %v", make_path, os_get_error_string(os_get_last_error()));
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        println("usage:");
        println("\ttouch [filename]");
        return 1;
    }

    os_init();

    strview_t filename = strv(argv[1]);

    if (os_file_exists(filename)) {
        err("file %v already exists!", filename);
        return 1;
    }

    oshandle_t fp = os_file_open(filename, FILEMODE_WRITE);
    // if we couldn't create the file, we need to create all the folders before it, ask 
    // the user if this is what they want to do
    if (!os_handle_valid(fp)) {
        print("the path to the file doesn't exist, do you want to create the intermediate folders? ");
        
        while (true) {
            print("y(es)/n(o): ");
            char c = (char)getchar();
            if (c == 'n') return 1;
            if (c == 'y') break;

            print("please input either y(es) or n(o)\n");
            getchar(); // consume newline
        }

        create_intermediate(filename);

        fp = os_file_open(filename, FILEMODE_WRITE);
    }

    os_file_close(fp);

    info("created %v", filename);
}
