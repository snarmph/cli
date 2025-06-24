#include "colla/build.c"


typedef void (*llm_callback_fn)(strview_t content, void *userdata);

typedef struct llm_desc_t llm_desc_t;
struct llm_desc_t {
    strview_t custom_model;
    strview_t api_key;
    strview_t question;
    bool streaming;
    llm_callback_fn streaming_cb;
    void *streaming_userdata;
};

str_t llm_ask(arena_t *arena, llm_desc_t *desc);

double strv_to_num(strview_t v) {
    instream_t in = istr_init(v);
    double num;
    return istr_get_num(&in, &num) ? num : -1.0;
}

typedef struct llm_model_t llm_model_t;
struct llm_model_t {
    str_t name;
    str_t id;
};

darr_define(llm_models_t, llm_model_t);

llm_models_t *llm_get_free_models(arena_t *arena, arena_t scratch) {
    llm_models_t *out = NULL;

    http_res_t res = http_get(&scratch, strv("https://openrouter.ai/api/v1/models"));
    json_t *json = json_parse_str(&scratch, res.body, JSON_DEFAULT);

    json_t *data = json_get(json, strv("data"));
    json_for(model, data) {
        // .pricing.prompt
        json_t *name    = json_get(model, strv("name"));
        json_t *id      = json_get(model, strv("id"));
        json_t *pricing = json_get(model, strv("pricing"));
        json_t *prompt  = json_get(pricing, strv("prompt"));

        if (json_check(prompt, JSON_STRING)) {
            double cost = strv_to_num(prompt->string);
            if (cost == 0.0) {
                llm_model_t info = {
                    .name = str(arena, name->string),
                    .id   = str(arena, id->string),
                };
                darr_push(arena, out, info);
            }
        }
    }

    debug("space used: %_$$$dB", arena_tell(&scratch));

    return out;
}

int main() {
    colla_init(COLLA_ALL);
    arena_t arena = arena_make(ARENA_VIRTUAL, GB(1));

    arena_t scratch = arena_scratch(&arena, MB(2));

    llm_models_t *free = llm_get_free_models(&arena, scratch);

    for_each (it, free) {
        for (usize i = 0; i < it->count; ++i) {
            info("%v || %v", it->items[i].name, it->items[i].id);
        }
    }
}
