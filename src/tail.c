#include "colla/build.c"

#if 0

-n --lines (default: 10)
-r --retry (default: false)


#endif

typedef struct options_t options_t;
struct options_t {
    bool retry;
    int lines;
    strview_t file;
};

options_t opt = { .lines = 10 };

void print_usage(void) {
    print("usage:\n");
    print("tail [options] <file>\n");
    print("options:\n");
    print("\t-n --lines  [x]  number of lines to print, default: 10\n");
    print("\t-r --retry       should retry if it can't open the file, default: false");
}

#define IS_OPT(s, l) strv_equals(arg, strv(s)) || strv_equals(arg, strv(l))

void load_options(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        os_abort(1);
    }

    for (int i = 1; i < argc; ++i) {
        strview_t arg = strv(argv[i]);
        if (IS_OPT("-n", "--lines")) {
            if ((i + 1) >= argc) {
                fatal("passed options %v without argument", arg);
            }
            instream_t in = istr_init(strv(argv[i + 1]));
            if (!istr_get_i32(&in, &opt.lines)) {
                fatal("failed to parse number: %s", in.beg);
            }
        }
        else if (IS_OPT("-r", "--retry")) {
            opt.retry = true;
        }
        else {
            opt.file = arg; 
        }
    }
}

typedef struct ctx_t ctx_t;
struct ctx_t {
    str_t last_content;
    strview_t *last_lines;
};

ctx_t ctx = {0};

void print_content(arena_t scratch) {
    oshandle_t fp = os_file_open(opt.file, FILEMODE_READ);

    u8 buffer[1024];
    arena_t cache = arena_make(ARENA_STATIC, sizeof(buffer), buffer);

    usize size = os_file_size(fp);

    for (int i = 0; i < opt.lines; ++i) {
        usize off = sizeof(buffer) * i;
        if (off > size) {
            if ((sizeof(buffer) * (i-1)) > size) {
                break;
            }
            off = size;
        }
        
        os_file_seek(fp, size - sizeof(buffer) * i);
        usize read = os_file_read(fp, buffer, sizeof(buffer));
        
        instream_t in = istr_init(strv((char*)buffer, read));
        
    }

    while (!os_file_is_finished(fp)) {
        usize read = os_file_read(fp, buffer, sizeof(buffer));
        if (read == 0) break;



        // finished
        if (read < sizeof(buffer)) {
        }
    }

    os_file_close(fp);
}

int main(int argc, char **argv) {
    os_init();

    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    load_options(argc, argv);

    if (!os_file_exists(opt.file)) {
        fatal("file %v doesn't exist", opt.file);
    }

    ctx.last_lines = alloc(&arena, strview_t, opt.lines);

    print_content(arena);

    tstr_t full_path_tstr = os_file_fullpath(&arena, opt.file);
    str_t full_path = str_from_tstr(&arena, full_path_tstr);
    strview_t dir;
    os_file_split_path(strv(full_path), &dir, NULL, NULL);

    tstr_t tdir = strv_to_tstr(&arena, dir);

    HANDLE file = CreateFile(
        tdir.buf,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (file == INVALID_HANDLE_VALUE) {
        fatal("> %v", os_get_error_string(os_get_last_error()));
    }

    OVERLAPPED ov = {
        .hEvent = CreateEvent(NULL, FALSE, 0, NULL)
    };

    u8 changebuf[1024];
    BOOL success = ReadDirectoryChangesW(
        file,
        changebuf, sizeof(changebuf),
        TRUE,
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL,
        &ov,
        NULL
    );

    if (!success) {
        fatal("> %v", os_get_error_string(os_get_last_error()));
    }

    while (true) {
        arena_t scratch = arena;

        DWORD res = WaitForSingleObject(ov.hEvent, INFINITE);
        if (res != WAIT_OBJECT_0) {
            fatal("> %v", os_get_error_string(os_get_last_error()));
        }

        DWORD bytes;
        GetOverlappedResult(file, &ov, &bytes, FALSE);

        FILE_NOTIFY_INFORMATION *event = (FILE_NOTIFY_INFORMATION *)changebuf;

        while (true) {
            DWORD name_len = (event->FileNameLength / sizeof(WCHAR)) - 1;

            if (event->Action == FILE_ACTION_MODIFIED) {
                str_t changed = str_from_str16(&scratch, str16_init(event->FileName, name_len));
                info("file %v changed", changed);
            }

            if (event->NextEntryOffset) {
                u8 *buf = (u8 *)event;
                buf += event->NextEntryOffset;
                event = (FILE_NOTIFY_INFORMATION *)buf;
            }
            else {
                break;
            }
        }
    }

    // when update happens, print it to stdout
}
