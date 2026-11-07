#ifndef LANGUAGE_H
#define LANGUAGE_H

typedef enum {
    LANG_EN = 0,
    LANG_JA,
    LANG_COUNT
} OutputLang;

extern OutputLang current_lang;
extern OutputLang compile_lang;

#endif
