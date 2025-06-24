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
//
// #define ICON_FOLDER      ""
// #define ICON_FILE        ""
// #define ICON_CODE        "" 
// #define ICON_BIN         ""
// #define ICON_ZIP         ""
// #define ICON_IMG         ""
// #define ICON_MOV         "󰸬"
// #define ICON_MUSIC       "󰸪"
// #define ICON_CONF        "󱁼"
// #define ICON_TEXT        ""
//
#endif

#define ICONMAP_SIZE 128 

typedef enum {
    ICON_NONE,

    ICON_FOLDER,
    ICON_FILE,
    ICON_CODE,
    ICON_BIN,
    ICON_ZIP,
    ICON_IMG,
    ICON_MOV,
    ICON_MUSIC,
    ICON_CONF,
    ICON_TEXT,

    ICON__COUNT,
} icons_e;

typedef enum {
    ICON_STYLE_NONE,
    ICON_STYLE_NERD,
    ICON_STYLE_EMOJI,

    ICON_STYLE__COUNT,
} icon_style_e;

strview_t icons[ICON_STYLE__COUNT][ICON__COUNT] = {
    [ICON_STYLE_NONE] = {

        [ICON_FOLDER] = cstrv(">"),
        [ICON_FILE]   = cstrv("-"),
        [ICON_CODE]   = cstrv("-"),
        [ICON_BIN]    = cstrv("@"),
        [ICON_ZIP]    = cstrv("z"),
        [ICON_IMG]    = cstrv("*"),
        [ICON_MOV]    = cstrv("v"),
        [ICON_MUSIC]  = cstrv("m"),
        [ICON_CONF]   = cstrv("c"),
        [ICON_TEXT]   = cstrv("t"),

    },
    [ICON_STYLE_NERD] = {
 
        [ICON_FOLDER] = cstrv(""),
        [ICON_FILE]   = cstrv(""),
        [ICON_CODE]   = cstrv(""),
        [ICON_BIN]    = cstrv(""),
        [ICON_ZIP]    = cstrv(""),
        [ICON_IMG]    = cstrv(""),
        [ICON_MOV]    = cstrv("󰸬"),
        [ICON_MUSIC]  = cstrv("󰸪"),
        [ICON_CONF]   = cstrv("󱁼"),
        [ICON_TEXT]   = cstrv(""),

    },
    [ICON_STYLE_EMOJI] = {
 
        [ICON_FOLDER] = cstrv("📂"),
        [ICON_FILE]   = cstrv("📄"),
        [ICON_CODE]   = cstrv("💻"),
        [ICON_BIN]    = cstrv("🤖"),
        [ICON_ZIP]    = cstrv("📦"),
        [ICON_IMG]    = cstrv("📸"),
        [ICON_MOV]    = cstrv("🎥"),
        [ICON_MUSIC]  = cstrv("🎧"),
        [ICON_CONF]   = cstrv("🔧"),
        [ICON_TEXT]   = cstrv("📑"),

    },
};

typedef struct icons_map_t icons_map_t;
struct icons_map_t {
    icon_style_e style;
    strview_t keys[ICONMAP_SIZE];
    strview_t values[ICONMAP_SIZE];
    u64 hashes[ICONMAP_SIZE];
};

icons_map_t icon__map = {0};

void icons_init(icon_style_e style);
void iconsmap__add(strview_t key, strview_t ico);
strview_t iconsmap__get(strview_t key);
u64 iconsmap__hash(strview_t key);

strview_t ext_to_ico(strview_t ext) {
    return iconsmap__get(ext); 
}

// ICONS HASHMAP IMPLEMENTATION //////////////////////////////

void iconsmap__add(strview_t key, strview_t ico) {
    u64 hash = iconsmap__hash(key);
    usize index = hash & (ICONMAP_SIZE - 1);

    for (usize i = index; i < ICONMAP_SIZE; ++i) {
        if (icon__map.hashes[i] == 0) {
            icon__map.hashes[i] = hash;
            icon__map.keys[i] = key;
            icon__map.values[i] = ico;
            return;
        }
    }

    for (usize i = 0; i < index; ++i) {
        if (icon__map.hashes[i] == 0) {
            icon__map.hashes[i] = hash;
            icon__map.keys[i] = key;
            icon__map.values[i] = ico;
            return;
        }
    }

    fatal("could not find an empty slot in icons hashmap, increase ICONMAP_SIZE");
}

strview_t iconsmap__get(strview_t key) {
    if (strv_is_empty(key)) {
        return icons[icon__map.style][ICON_FILE];
    }

    u64 hash = iconsmap__hash(key);
    usize index = hash & (ICONMAP_SIZE - 1);
    for (usize i = index; i < ICONMAP_SIZE; ++i) {
        if (icon__map.hashes[i] == hash && 
            strv_equals(icon__map.keys[i], key))
        {
            return icon__map.values[i];
        }

        if (icon__map.hashes[i] == 0) {
            return icons[icon__map.style][ICON_FILE];
        }
    }

    for (usize i = 0; i < index; ++i) {
        if (icon__map.hashes[i] == hash && 
            strv_equals(icon__map.keys[i], key))
        {
            return icon__map.values[i];
        }

        if (icon__map.hashes[i] == 0) {
            return icons[icon__map.style][ICON_FILE];
        }
    }

    // return a default icon for unrecognized files
    return icons[icon__map.style][ICON_FILE];
}

u64 iconsmap__hash(strview_t key) {
    const u8 *data = (const u8 *)key.buf;
    u64 hash = 0;

    for (usize i = 0; i < key.len; ++i) {
		hash = data[i] + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

void icons_init(icon_style_e style) {
    icon__map.style = style;

    // add all the known icons here!
#define add_ico(ext, ico) iconsmap__add((strview_t)cstrv(ext), icons[style][ICON_##ico])

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


