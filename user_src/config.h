// ============================================================================
// TCC CONFIG — Fake config.h для кросс-компиляции TinyCC в Enclave OS
// Этот файл подменяет собой автогенерируемый config.h от ./configure
// ============================================================================
#ifndef TCC_CONFIG_H
#define TCC_CONFIG_H

/* Target Architecture (32-bit x86) */
#define TCC_TARGET_I386 1

/* Version (должна совпадать с версией скачанного исходника) */
#define TCC_VERSION "0.9.27"

/* Static linking only (отключает dlopen/dlsym) */
#define CONFIG_TCC_STATIC 1

/* ==========================================================================
 * Пути внутри VFS Enclave OS (КРИТИЧНО!)
 * TinyCC будет искать заголовки и библиотеки по этим путям при компиляции
 * программ ВНУТРИ нашей ОС.
 * ========================================================================== */
#define CONFIG_TCCDIR "/lib"
#define CONFIG_TCC_SYSINCLUDEPATHS "/include:/usr/include"
#define CONFIG_TCC_LIBPATHS "/lib:/usr/lib"
#define CONFIG_TCC_CRTPREFIX "/lib"
/* У нас нет динамического линкера, поэтому интерпретатор пустой */
#define CONFIG_TCC_ELFINTERP ""
#define CONFIG_LDDIR "lib"

/* Triplet (используется для поиска кросс-компиляторных путей) */
#define HOST_TRIPLET "i386-pc-enclaveos"
#define TRIPLET "i386-pc-enclaveos"

/* Отключаем фичи, которые требуют хостовой ОС или не поддерживаются */
#define CONFIG_TCC_SEMLOCK 0      /* Нет семафоров в Ring 3 */
#define CONFIG_TCC_BACKTRACE 0    /* Нет поддержки backtrace */
#define CONFIG_TCC_BCHECK 0       /* Bounds checking требует спец. либ */
#define CONFIG_TCC_USE_LIBGCC 0   /* Мы используем libtcc1.a вместо libgcc */

/* Предотвращаем попытки tcc использовать mmap для всего подряд */
/* (хотя sys_mmap у нас есть, лучше ограничить) */
#define CONFIG_TCC_MMAP 0

#endif
