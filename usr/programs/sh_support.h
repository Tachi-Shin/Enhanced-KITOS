#ifndef SH_SUPPORT_H
#define SH_SUPPORT_H
#include "lineedit.h"
extern struct sh_history sh_history;
void sh_history_load(void);
void sh_history_save(void);
void sh_history_command(int argc,char **argv);
int sh_complete(void *,const char *,size_t,struct sh_completion *);
#endif
