#include "colla/build.c"

typedef enum {
    LLM_CUSTOM,
    LLM_GOOGLE_GEMMA,
    LLM_MISTRAL_7B,
    LLM_META_LLAMA_33,
    LLM_QWEN_QWQ,
    LLM_QWEN_25,
    LLM_META_LLAMA_32,
    LLM_GOOGLE_GEMINI,
    LLM_OPENGV_LAB,
    LLM__COUNT,
} llm_models_e;

typedef void (*llm_callback_fn)(strview_t content, void *userdata);

typedef struct llm_desc_t llm_desc_t;
struct llm_desc_t {
    llm_models_e model;
    strview_t custom_model;
    strview_t api_key;
    strview_t question;
    bool streaming;
    llm_callback_fn streaming_cb;
    void *streaming_userdata;
};

str_t llm_ask(arena_t *arena, llm_desc_t *desc);


strview_t llm__model_name[LLM__COUNT] = {
    [LLM_GOOGLE_GEMMA]  = cstrv("google/gemma-3n-e4b-it:free"),
    [LLM_MISTRAL_7B]    = cstrv("mistralai/mistral-7b-instruct:free"),
    [LLM_META_LLAMA_33] = cstrv("meta-llama/llama-3.3-8b-instruct:free"),
    [LLM_QWEN_QWQ]      = cstrv("qwen/qwq-32b:free"),
    [LLM_QWEN_25]       = cstrv("qwen/qwen-2.5-7b-instruct:free"),
    [LLM_META_LLAMA_32] = cstrv("meta-llama/llama-3.2-3b-instruct:free"),
    [LLM_GOOGLE_GEMINI] = cstrv("google/gemini-2.0-flash-exp:free"),
    [LLM_OPENGV_LAB]    = cstrv("opengvlab/internvl3-2b:free"),
};
