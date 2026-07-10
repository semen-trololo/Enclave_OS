#ifndef TMPFS_H
#define TMPFS_H

#include "vfs.h"

// Инициализация и монтирование tmpfs в /tmp
void tmpfs_init(void);

// Глобальная нода корня /tmp (нужна для syscall.c)
extern vfs_node_t* tmpfs_root;

#endif
