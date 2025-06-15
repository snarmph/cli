#pragma once

#include "colla/str.h"
#include "colla/os.h"

#if ICONS_DISABLED

#define ICON_FOLDER      ">" 
#define ICON_FILE        "-" 
#define ICON_CODE        "-" 
#define ICON_BIN         "@" 
#define ICON_ZIP         "z" 
#define ICON_IMG         "*" 
#define ICON_MOV         "v" 
#define ICON_MUSIC       "m" 
#define ICON_CONF        "c" 
#define ICON_TEXT        "t" 

#else

#define ICON_FOLDER      ""
#define ICON_FILE        ""
#define ICON_CODE        "" 
#define ICON_BIN         ""
#define ICON_ZIP         ""
#define ICON_IMG         ""
#define ICON_MOV         "󰸬"
#define ICON_MUSIC       "󰸪"
#define ICON_CONF        "󱁼"
#define ICON_TEXT        ""

#endif

#define ICONMAP_SIZE 128 

typedef struct icons_map_t icons_map_t;
struct icons_map_t {
    strview_t keys[ICONMAP_SIZE];
    const char *values[ICONMAP_SIZE];
    u64 hashes[ICONMAP_SIZE];
};

icons_map_t map = {0};

void icons_init(void);
void iconsmap__add(strview_t key, const char *ico);
const char *iconsmap__get(strview_t key);
u64 iconsmap__hash(strview_t key);

const char *ext_to_ico(strview_t ext) {
    return iconsmap__get(ext); 
}

// ICONS HASHMAP IMPLEMENTATION //////////////////////////////

void iconsmap__add(strview_t key, const char *ico) {
    u64 hash = iconsmap__hash(key);
    usize index = hash & (ICONMAP_SIZE - 1);

    for (usize i = index; i < ICONMAP_SIZE; ++i) {
        if (map.hashes[i] == 0) {
            map.hashes[i] = hash;
            map.keys[i] = key;
            map.values[i] = ico;
            return;
        }
    }

    for (usize i = 0; i < index; ++i) {
        if (map.hashes[i] == 0) {
            map.hashes[i] = hash;
            map.keys[i] = key;
            map.values[i] = ico;
            return;
        }
    }

    fatal("could not find an empty slot in icons hashmap, increase ICONMAP_SIZE");
}

const char *iconsmap__get(strview_t key) {
    if (strv_is_empty(key)) {
        return ICON_FILE;
    }

    u64 hash = iconsmap__hash(key);
    usize index = hash & (ICONMAP_SIZE - 1);
    for (usize i = index; i < ICONMAP_SIZE; ++i) {
        if (map.hashes[i] == hash && 
            strv_equals(map.keys[i], key))
        {
            return map.values[i];
        }

        if (map.hashes[i] == 0) {
            return ICON_FILE;
        }
    }

    for (usize i = 0; i < index; ++i) {
        if (map.hashes[i] == hash && 
            strv_equals(map.keys[i], key))
        {
            return map.values[i];
        }

        if (map.hashes[i] == 0) {
            return ICON_FILE;
        }
    }

    // return a default icon for unrecognized files
    return ICON_FILE;
}

u64 iconsmap__hash(strview_t key) {
    const u8 *data = (const u8 *)key.buf;
    u64 hash = 0;

    for (usize i = 0; i < key.len; ++i) {
		hash = data[i] + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

void icons_init(void) {
    // add all the known icons here!
#define add_ico(ext, ico) iconsmap__add((strview_t)cstrv(ext), ICON_##ico)

    // CODE ICONS

    add_ico(".h", CODE);
    add_ico(".hh", CODE);
    add_ico(".hpp", CODE);
    add_ico(".c", CODE);
    add_ico(".cc", CODE);
    add_ico(".cpp", CODE);
    add_ico(".c++", CODE);

    add_ico(".go", CODE);
    add_ico(".rs", CODE);
    add_ico(".lua", CODE);
    add_ico(".cs", CODE);
    add_ico(".css", CODE);
    add_ico(".js", CODE);
    add_ico(".html", CODE);
    add_ico(".glsl", CODE);
    add_ico(".hlsl", CODE);
    add_ico(".fs", CODE);
    add_ico(".ps", CODE);
    add_ico(".vs", CODE);
    add_ico(".vert", CODE);
    add_ico(".frag", CODE);
    add_ico(".gml", CODE);
    add_ico(".java", CODE);
    add_ico(".sh", CODE);
    add_ico(".php", CODE);
    add_ico(".py", CODE);
    add_ico(".rb", CODE);
    add_ico(".swift", CODE);
    add_ico(".", CODE);

    add_ico(".bat", CODE);
    add_ico(".cmd", CODE);
    add_ico(".ps1", CODE);

    // BINARY ICONS
    
    add_ico(".bin", BIN);

    
    // ZIP ICONS
    
    add_ico(".ar", ZIP);
    add_ico(".iso", ZIP);
    add_ico(".tar", ZIP);
    add_ico(".br", ZIP);
    add_ico(".bz2", ZIP);
    add_ico(".gz", ZIP);
    add_ico(".gzip", ZIP);
    add_ico(".lz", ZIP);
    add_ico(".lz4", ZIP);
    add_ico(".7z", ZIP);
    add_ico(".dmg", ZIP);
    add_ico(".jar", ZIP);
    add_ico(".rar", ZIP);
    add_ico(".tar", ZIP);
    add_ico(".zip", ZIP);

    
    // IMAGE ICONS
    
    add_ico(".jpeg", IMG);
    add_ico(".jpg", IMG);
    add_ico(".gif", IMG);
    add_ico(".png", IMG);
    add_ico(".webp", IMG);
    add_ico(".avif", IMG);
    add_ico(".tiff", IMG);
    add_ico(".bmp", IMG);
    add_ico(".ico", IMG);
    add_ico(".pdn", IMG);
    add_ico(".psd", IMG);
    add_ico(".sai", IMG);
    add_ico(".tga", IMG);
    add_ico(".svg", IMG);

    // VIDEO ICONS
    
    add_ico(".mkv", MOV);
    add_ico(".ogv", MOV);
    add_ico(".avi", MOV);
    add_ico(".mov", MOV);
    add_ico(".mp4", MOV);
    add_ico(".mpv", MOV);

    
    // AUDIO ICONS
    
    add_ico(".aiff", MUSIC);
    add_ico(".flac", MUSIC);
    add_ico(".wav", MUSIC);
    add_ico(".mp3", MUSIC);
    add_ico(".ogg", MUSIC);
    
    // CONF FILE ICONS
    
    add_ico(".ini", CONF);
    add_ico(".toml", CONF);
    add_ico(".conf", CONF);
    add_ico(".json", CONF);
    add_ico(".xml", CONF);
    add_ico(".md", CONF);
    add_ico(".yaml", CONF);

    
    // TEXT ICONS
    add_ico(".txt", CONF);

#undef add_ico
}


