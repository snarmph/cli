#include "colla/build.c" 
#include "icons.h"
#include "term.c"

#define ATOMIC_SET(v, x) (InterlockedExchange(&v, (x)))
#define ATOMIC_CHECK(v)  (InterlockedCompareExchange(&v, 1, 1))
#define ATOMIC_INC(v) (InterlockedIncrement(&v))
#define ATOMIC_DEC(v) (InterlockedDecrement(&v))
#define ATOMIC_GET(v) (InterlockedOr(&v, 0))

typedef struct options_t options_t;
struct options_t {
    bool case_sensitive;
    bool exact_name;
    bool all_dirs;
    int j;
    str_t dir;
    str_t tofind;
    str_t tofind_original;
};

// == GLOBALS =============
strview_t CURDIR  = { .buf = ".",  .len = 1 };
strview_t PREVDIR = { .buf = "..", .len = 2 };

volatile long checked_count = 0;
volatile long found_count = 0;
options_t opt = {0};

int usage(void) {
    print("usage: find [dir] <filename>\n");
    print("options:\n");
    print("\t-d / -dir         search directory\n");
    print("\t-s / -sensitive   case sensitive\n");
    print("\t-e / -exact       exact filename\n");
    print("\t-a / -all         check all directory, even ones that start with a dot\n");
    print("\t-j                number of threads (default: 4)\n");
    return 1;
}

options_t get_options(arena_t *arena, int argc, char **argv) {
    options_t out = {
        .j = 4,
    };

#define IS_OPT(short, long) strv_equals(arg, strv(short)) || strv_equals(arg, strv(long))

    for (int i = 1; i < argc; ++i) {
        strview_t arg = strv(argv[i]);
        if (IS_OPT("-d", "-dir")) {
            if ((i + 1) >= argc) {
                err("wrong usage");
                usage();
                os_abort(1);
            }
            strview_t path = strv(argv[++i]);
            if (!strv_ends_with(path, '/') && !strv_ends_with(path, '\\')) {
                out.dir = str_fmt(arena, "%v/", path);
            }
            else {
                out.dir = str(arena, path);
            }
        }
        else if (IS_OPT("-s", "-sensitive")) {
            out.case_sensitive = true;
        }
        else if (IS_OPT("-e", "-exact")) {
            out.exact_name = true;
        }
        else if (IS_OPT("-a", "-all")) {
            out.all_dirs = true;
        }
        else if(strv_equals(arg, strv("-j"))) {
            ++i;
            if (i >= argc) {
                fatal("passed option -j without any number afterwards");
            }
            arg = strv(argv[i]);
            instream_t istr = istr_init(arg);
            istr_get_i32(&istr, &out.j);
        }
        else {
            if (!str_is_empty(out.tofind)) {
                fatal("passed multiple files to search: (%v) and (%v)", out.tofind, arg);
            }
            out.tofind = str(arena, arg);
        }
    }

    if (str_is_empty(out.dir)) {
        out.dir = str(arena, "./");
    }

    if (str_is_empty(out.tofind)) {
        fatal("no files passed");
    }

#undef IS_OPT

    if (out.j == 0) {
        out.j = 1;
    }

    return out;
}

typedef struct jobdata_t jobdata_t;
struct jobdata_t {
    str_t path;
    usize alloc_len;
    jobdata_t *next;
    jobdata_t *prev;
};

typedef struct result_t result_t;
struct result_t {
    strview_t icon;
    str_t before;
    str_t name;
    str_t after;
};

darr_define(resarr_t, result_t);

typedef struct worker_t worker_t;
struct worker_t {
    arena_t arena;
    arena_t scratch;
    resarr_t *results;
    jobdata_t *jobs;
};

oshandle_t threads[64] = {0};
worker_t data[64] = {0};

jobdata_t *jobs = NULL;
jobdata_t *freelist = NULL;
oshandle_t jobs_mutex = {0};
volatile long should_quit = false;

oshandle_t job_notif = {0};

volatile long jobs_in_progess = 0;

oshandle_t print_mtx = {0};

#define PRINT(...) do { os_mutex_lock(print_mtx); pretty_print(scratch, __VA_ARGS__); os_mutex_unlock(print_mtx); } while (0)

void try_add_path(worker_t *data, strview_t path, strview_t name, bool is_dir) {
    arena_t before = data->arena;

    arena_t scratch = data->scratch;

    ATOMIC_INC(checked_count);

    strview_t current = name;
    if (!opt.case_sensitive) {
        str_t filename = str(&scratch, current);
        str_upper(&filename);
        current = strv(filename);
    }

    result_t res = { .before = str(&data->arena, path) };

    usize index = 0;

    if (opt.exact_name) {
        if (!strv_ends_with_view(current, strv(opt.tofind))) {
            return;
        }

        index = current.len - opt.tofind.len;
    }
    else {
        index = strv_find_view(current, strv(opt.tofind), 0);
        if (index == STR_NONE) {
            return;
        }
    }

    //res.name = strv_sub(name, 0, index);
    res.name = str(&data->arena, strv_sub(name, 0, index));
    res.after = str(&data->arena, strv_sub(name, index + opt.tofind.len, SIZE_MAX));
    
    if (res.before.buf[0] == '.' &&
            (res.before.buf[1] == '/' ||
             res.before.buf[1] == '\\')
       ) {
        // res.before = str(&data->arena, strv_remove_prefix(res.before, 2));
        res.before.buf += 2;
        res.before.len -= 2;
    }

    if (is_dir) {
        res.icon = icons[ICON_STYLE_NERD][ICON_FOLDER];
    }
    else {
        strview_t ext;
        os_file_split_path(name, NULL, NULL, &ext);
        res.icon = ext_to_ico(ext);
    }
    
    ATOMIC_INC(found_count);

#if 0
    PRINT(
        "<green>%v</> <grey>%v<yellow>%v<green>%v<yellow>%v</>\n",
        res.icon,
        res.before,
        res.name,
        opt.tofind_original,
        res.after
    );
#endif

    data->arena = before;
    // darr_push(&data->arena, data->results, res);
}

void add_dirs(worker_t *data, strview_t path) {
    arena_t scratch = data->scratch;
    dir_t *dir = os_dir_open(&scratch, path);
    // dir_t *dir = os_dir_open(&data->arena, path);

    // dir_foreach(&data->arena, entry, dir) {
    dir_foreach(&scratch, entry, dir) {
        if (strv_equals(strv(entry->name), CURDIR) ||
            strv_equals(strv(entry->name), PREVDIR))
        {
            continue;
        }

        if (entry->type == DIRTYPE_DIR) {
            if (!opt.all_dirs && entry->name.buf[0] == '.') {
                continue;
            }

            // jobdata_t *newjob = alloc(&data->arena, jobdata_ t);
            // newjob->path = strv(fullpath);

            str_t fullpath = str_fmt(&scratch, "%v%v/", path, entry->name);

            os_mutex_lock(jobs_mutex);
            
            jobdata_t *newjob = freelist;
            
            if (newjob) {
                dlist_pop(freelist, newjob);
                if (newjob->alloc_len >= fullpath.len) {
                    memcpy(newjob->path.buf, fullpath.buf, fullpath.len);
                    newjob->path.len = fullpath.len;
                }
            }
            else {
                newjob = alloc(&data->arena, jobdata_t);
                newjob->path = str_dup(&data->arena, fullpath);
                newjob->alloc_len = fullpath.len;
            }

            // newjob->path = strv(fullpath);

            dlist_push(jobs, newjob);
                
            os_mutex_unlock(jobs_mutex);

            os_cond_signal(job_notif);
        }

        try_add_path(data, path, strv(entry->name), entry->type == DIRTYPE_DIR); 
    }
}

int worker(u64 id, void *udata) {
    COLLA_UNUSED(id);

    worker_t *data = udata;
    
    while (!ATOMIC_CHECK(should_quit)) {
        if (!os_mutex_try_lock(jobs_mutex)) {
            continue;
        }

        while (!jobs && !ATOMIC_CHECK(should_quit)) {
            os_cond_wait(job_notif, jobs_mutex, OS_WAIT_INFINITE);
        }

        jobdata_t *job = jobs;
        dlist_pop(jobs, job);
        
        ATOMIC_INC(jobs_in_progess);
        
        os_mutex_unlock(jobs_mutex);

        if (job) {
            add_dirs(data, strv(job->path));

            os_mutex_lock(jobs_mutex);
                dlist_push(freelist, job);
            os_mutex_unlock(jobs_mutex);
        }

        ATOMIC_DEC(jobs_in_progess);
    }

    return 0;
}

// int loading_thread(u64 id, void *udata) {
//     COLLA_UNUSED(id);
//     COLLA_UNUSED(udata);
//
//     const int frame_time = 3600;
//
//     char loading_anim[] = "|/-\\|/-\\";
//     int loading_timer = frame_time;
//     int frame = 0;
//
//     while(!ATOMIC_CHECK(should_quit)) {
//         loading_timer--;
//         if (loading_timer <= 0) {
//             frame = (frame + 1) % (arrlen(loading_anim) - 1);
//             loading_timer = frame_time;
//         }
//
//         print(" %c files checked: %d\r", loading_anim[frame], ATOMIC_GET(checked_count));
//     }
//
//     return 0;
// }

struct {
    spinner_t spinner;
    bool finished;
    bool should_print;
} app = {0};

bool app_update(arena_t *arena, float dt, void *udata) {
    if (app.finished) return true;

    spinner_update(&app.spinner, dt);

    if (!os_mutex_try_lock(jobs_mutex)) {
        return false;
    }

    bool finished = ATOMIC_GET(jobs_in_progess) <= 0 && !jobs;

    os_mutex_unlock(jobs_mutex);

    if (!finished) return false;

    ATOMIC_SET(should_quit, 1);
    os_cond_broadcast(job_notif);

    if (app.should_print) return true;
    
    for (int i = 0; i < opt.j; ++i) {
        if (!os_thread_join(threads[i], NULL)) {
            fatal("%d wait failed: %v", i, os_get_error_string(os_get_last_error()));
        }
    }

    //os_wait_t res = os_wait_on_handles(threads, opt.j, true, INFINITE);
    //if (res.result == OS_WAIT_FAILED) {
        //fatal("wait failed: %v", os_get_error_string(os_get_last_error()));
    //}
    //app.should_print = res.result == OS_WAIT_FINISHED;
    app.should_print = true;
    return false;
}

void app_event(termevent_t *e, void *udata) {
    if (e->type == TERM_EVENT_KEY &&
        strv_equals(strv(e->value), strv("q"))
       ) {

        // for (usize i = 0; i < opt.j; ++i) {
        //     arena__print_crash(&data[i].arena);
        //     arena__print_crash(&data[i].scratch);
        // }
    
        app.finished = true;
    }
    // return false;
    
    // return app.finished;
}

str_t app_view(arena_t *arena, void *udata) {
    outstream_t out = ostr_init(arena);

    if (app.should_print) {
        for (int i = 0; i < opt.j; ++i) {
            for_each (r, data[i].results) {
                for (usize k = 0; k < r->count; ++k) {
                    result_t *res = &r->items[k];

                    ostr_print(
                        &out, 
                        "<green>%v</> <grey>%v<yellow>%v<green>%v<yellow>%v</>\n",
                        res->icon,
                        res->before,
                        res->name,
                        opt.tofind_original,
                        res->after
                    );
                }
            }
        }

        ostr_print(&out, "\nfound %d/%d", found_count, checked_count);
        app.finished = true;
    }
    else {
        ostr_print(
            &out,
            "<magenta>%v</> files checked: %d", 
            app.spinner.frames[app.spinner.cur], 
            ATOMIC_GET(checked_count)
        );
    }

    return ostr_to_str(&out);
    // return STR_EMPTY;
}

int main(int argc, char **argv) {
    printf("> %d\n", COLLA_DEBUG);
    if (argc < 2) return usage();

    colla_init(COLLA_OS | COLLA_CORE);
    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    icons_init(ICON_STYLE_NERD);

    opt = get_options(&arena, argc, argv);
   
    opt.tofind_original = str_dup(&arena, opt.tofind);

    if (!opt.case_sensitive) {
        str_upper(&opt.tofind);
    }

    print_mtx = os_mutex_create();

    jobs_mutex = os_mutex_create();

    job_notif = os_cond_create();

    jobdata_t *initial_job = alloc(&arena, jobdata_t);
    initial_job->path = str_dup(&arena, opt.dir);
    initial_job->alloc_len = opt.dir.len;
    dlist_push(jobs, initial_job);

    for (int i = 0; i < opt.j; ++i) {
        data[i].arena = arena_make(ARENA_VIRTUAL, GB(1));
        data[i].scratch = arena_make(ARENA_VIRTUAL, GB(1));
        threads[i] = os_thread_launch(worker, &data[i]);
    }

    app.spinner = spinner_init(SPINNER_DOT);

    term_init(&(termdesc_t){
        .app = {
            .update = app_update,
            .event = app_event,
            .view = app_view,
        },
    });

    term_run();

#if 0
    opt.j += 1;
    
    threads[0] = os_thread_launch(loading_thread, NULL);

    for (int i = 1; i < opt.j; ++i) {
        data[i].arena = arena_make(ARENA_VIRTUAL, GB(1));
        threads[i] = os_thread_launch(worker, &data[i]);
    }

    bool finished = false;

    while (!finished) {
        if (!os_mutex_try_lock(jobs_mutex)) {
            continue;
        }

        finished = ATOMIC_GET(jobs_in_progess) <= 0 && !jobs;

        os_mutex_unlock(jobs_mutex);
    }

    ATOMIC_SET(should_quit, 1);

    os_cond_broadcast(job_notif);

    // clear loading line
    print("\33[2K\r");

    for (int i = 0; i < opt.j; ++i) {
        os_thread_join(threads[i], NULL);

        for_each(r, data[i].results) {
            for (usize k = 0; k < r->count; ++k) {
                result_t *res = &r->items[k]; 

                os_log_set_colour(LOG_COL_GREEN);
                print("%v ", res->icon);
                os_log_set_colour(LOG_COL_GREY);
                print("%v", res->before);
                os_log_set_colour(LOG_COL_YELLOW);
                print("%v", res->name);
                os_log_set_colour(LOG_COL_GREEN);
                print("%v", opt.tofind_original);
                os_log_set_colour(LOG_COL_YELLOW);
                print("%v\n", res->after);
            }
        }
    }

    print("\n");
    println("found %d/%d", found_count, checked_count);
#endif
}
