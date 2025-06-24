#pragma once

#include "colla/core.h"
#include "colla/os.h"
#include "colla/str.h"

typedef enum {
    TERM_EVENT_NONE,
    TERM_EVENT_KEY,
    TERM_EVENT_MOUSE,
    TERM_EVENT_RESIZE,
    TERM_EVENT__COUNT,
} termevent_type_e;

typedef struct termevent_t termevent_t;
struct termevent_t {
    termevent_type_e type;
    bool is_key_down;
    str_t value;
};

typedef struct termapp_t termapp_t;
struct termapp_t {
    bool (*update)(arena_t *arena, float dt, void *userdata);
    str_t (*view)(arena_t *arena, void *userdata);
    void (*event)(termevent_t *event, void *userdata);
    void *userdata;
};

typedef struct termdesc_t termdesc_t;
struct termdesc_t {
    int fps;
    bool fullscreen;
    oshandle_t output;
    termapp_t app;
};

typedef struct term_t term_t;

void term_init(termdesc_t *desc);

void term_run(void);

void term_move(int offx, int offy);
void term_write(strview_t str);

/// COMPONENTS //////////////////////////////////////////

// -- spinner ---------------------------------------- //

typedef enum {
    SPINNER_LINE,
    SPINNER_DOT,
    SPINNER_MINIDOT,
    SPINNER_JUMP,
    SPINNER_PULSE,
    SPINNER_POINTS,
    SPINNER_GLOBE,
    SPINNER_MOON,
    SPINNER_METER,
    SPINNER_ELLIPSIS,
    
    SPINNER__COUNT,
} spinner_type_e;

typedef struct spinner_t spinner_t;
struct spinner_t {
    strview_t *frames;
    int count;
    int cur;
    float timer;
    float passed;
};

spinner_t spinner_init(spinner_type_e type);

void spinner_update(spinner_t *ctx, float dt);
