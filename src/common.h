#pragma once

#include "colla/str.h"
#include "colla/arena.h"
#include "colla/parsers.h"
#include <windows.h>

str_t common_get_local_folder(arena_t *arena) {
#if COLLA_DEBUG
    return str(arena, "debug");
#else
    u8 tmpbuf[KB(5)];
    arena_t scratch = arena_make(ARENA_STATIC, sizeof(tmpbuf), tmpbuf);

    TCHAR tpath[512] = {0};

    DWORD len = GetModuleFileName(NULL, tpath, arrlen(tpath));

    str_t path = str_from_tstr(&scratch, tstr_init(tpath, len));
    
    strview_t dir;
    os_file_split_path(strv(path), &dir, NULL, NULL);

    return str(arena, dir);
#endif
}

ini_t common_get_config(arena_t *arena, str_t *out_conf_fname) {
    u8 tmpbuf[KB(5)];
    arena_t scratch = arena_make(ARENA_STATIC, sizeof(tmpbuf), tmpbuf);

    str_t local_dir = common_get_local_folder(&scratch);
    str_t conf_fname = str_fmt(&scratch, "%v/conf.ini", local_dir);
    ini_t conf = ini_parse(arena, strv(conf_fname), NULL);

    if (out_conf_fname) {
        *out_conf_fname = str_dup(arena, conf_fname);
    }

    return conf;
}

