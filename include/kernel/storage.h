#ifndef KERNEL_STORAGE_H
#define KERNEL_STORAGE_H
void storage_poll(void);
void storage_print(void);
int storage_mount(unsigned int index,const char *path);
#endif
