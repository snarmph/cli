#include "colla/build.c"

void print_usage(void) {
    print("usage:\n");
    print("time <command> [arguments]\n");
}

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

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    os_init();

    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    os_cmd_t *command = NULL;

    for (int i = 1; i < argc; ++i) {
        darr_push(&arena, command, strv(argv[i]));
    }

    i64 tps = get_ticks_per_second();
    i64 begin = get_ticks();
        os_run_cmd(arena, command, NULL);
    i64 end = get_ticks();

    double time_taken = (double)(end - begin) / (double)tps;

    print("\ntime taken: ");

    const char *suffix[] = {
        "seconds",
        "milliseconds",
        "microseconds"
        "nanoseconds",
    };

    int suffix_index = 0;

    while (time_taken < 1.0 && (suffix_index + 1) < arrlen(suffix)) {
        time_taken *= 1000.0;
        suffix_index++;
    }

    print("%.2f %s\n", time_taken, suffix[suffix_index]);
}
