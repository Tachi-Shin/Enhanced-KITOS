#ifndef SH_LINEEDIT_H
#define SH_LINEEDIT_H
#include <stddef.h>
#define SH_LINE_MAX 1024
#define SH_HISTORY_MAX 64
struct sh_history { char lines[SH_HISTORY_MAX][SH_LINE_MAX]; unsigned int first, count; };
struct sh_completion {
    size_t begin, end;
    char replacement[SH_LINE_MAX];
    char names[64][768];
    unsigned int count, total;
};
struct sh_edit_io {
    void *context;
    int (*read)(void *, char *);
    void (*write)(void *, const char *, size_t);
    void (*prompt)(void *);
    int (*complete)(void *, const char *, size_t, struct sh_completion *);
    unsigned int columns, prompt_columns;
};
const char *sh_history_get(const struct sh_history *, unsigned int);
void sh_history_add(struct sh_history *, const char *);
unsigned int sh_text_width(const char *, size_t);
/* 1=line, 0=EOF, -1=cancel, -2=overflow. Never executes a recalled/completed line. */
int sh_edit_read(struct sh_edit_io *, struct sh_history *, char *, size_t);
#endif
