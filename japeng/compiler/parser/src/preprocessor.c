#include "preprocessor.h"
#include <string.h>

extern LanguageFrontend ja_frontend;

static LanguageFrontend* g_frontends[] = {
    &ja_frontend,
    NULL // 番兵
};

static LanguageFrontend* g_active_frontend = NULL;

LanguageFrontend* find_frontend_by_ext(const char* ext) {
    if (!ext) return NULL;
    for (int i = 0; g_frontends[i] != NULL; i++) {
        if (strcmp(ext, g_frontends[i]->ext_script) == 0 ||
            strcmp(ext, g_frontends[i]->ext_class) == 0) {
            return g_frontends[i];
        }
    }
    return NULL;
}

void set_active_frontend(LanguageFrontend* frontend) {
    g_active_frontend = frontend;
}

LanguageFrontend* get_active_frontend(void) {
    return g_active_frontend;
}

char* frontend_preprocess(const char* src) {
    if (g_active_frontend && g_active_frontend->preprocess) {
        return g_active_frontend->preprocess(src);
    }
    return strdup(src);
}

void frontend_map_loc(int clean_line, int clean_col, int* out_line, int* out_col) {
    if (g_active_frontend && g_active_frontend->map_loc) {
        g_active_frontend->map_loc(clean_line, clean_col, out_line, out_col);
    } else {
        *out_line = clean_line;
        *out_col  = clean_col;
    }
}

void frontend_reset(void) {
    if (g_active_frontend && g_active_frontend->reset) {
        g_active_frontend->reset();
    }
}

const char* frontend_lookup_symbol(const char* placeholder) {
    if (g_active_frontend && g_active_frontend->lookup_symbol) {
        return g_active_frontend->lookup_symbol(placeholder);
    }
    return NULL;
}
