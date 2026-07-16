# ==============================================================================
# ENCLAVE OPERATING SYSTEM — Makefile (Single Source of Truth)
# Версия: Day 24 (PID 1 Architecture, Ring 3 Shell, Init Launcher)
# ==============================================================================

# ==============================================================================
# КОНФИГУРАЦИЯ И ИНСТРУМЕНТЫ
# ==============================================================================
CC = i686-linux-gnu-gcc
AS = nasm
USER_CC = i686-linux-gnu-gcc
AR = i686-linux-gnu-ar

# Директории
BUILD_DIR = build
ISO_DIR = $(BUILD_DIR)/isodir
INITRD_ROOT = $(BUILD_DIR)/initrd_root
USER_BIN_DIR = $(BUILD_DIR)/bin
USER_SBIN_DIR = $(BUILD_DIR)/sbin
USER_SRC_DIR = user_src
INITRD_SRC_DIR = initrd_src

# Имена файлов
ISO_NAME = $(BUILD_DIR)/enclave_os.iso
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
INITRD_TAR = $(BUILD_DIR)/initrd.tar
DISK_IMG = $(BUILD_DIR)/disk.img

# Флаги компиляции ядра (Строго по Базе Знаний)
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
CFLAGS += -fno-pie -fno-pic -fno-stack-protector
CFLAGS += -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -mincoming-stack-boundary=2 -g
CFLAGS += -MMD -MP

ASFLAGS = -f elf32 -g
LDFLAGS = -T linker.ld -nostdlib -no-pie -lgcc

# Флаги компиляции user-space
USER_CFLAGS = -m32 -nostdlib -static -ffreestanding -O2 -Wall -Wextra -fno-optimize-sibling-calls -Iuser_src
USER_LDFLAGS = -nostdlib -static -no-pie -T $(USER_SRC_DIR)/user_linker.ld

# ==============================================================================
# ИСХОДНИКИ И ОБЪЕКТЫ ЯДРА
# ==============================================================================
C_SOURCES = $(wildcard *.c)
ASM_SOURCES = $(wildcard *.asm)

# ✅ [ДЕНЬ 24] Исключаем test_runner.c из сборки ядра (файл удалён из архитектуры)
#    Если файл физически ещё существует, Makefile его проигнорирует.
KERNEL_EXCLUDE = test_runner.c
C_SOURCES_FILTERED = $(filter-out $(KERNEL_EXCLUDE),$(C_SOURCES))

C_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES_FILTERED))
ASM_OBJS = $(patsubst %.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

# boot.o ОБЯЗАТЕЛЬНО первым (Multiboot header должен быть в первых 8KB)
OBJ = $(BUILD_DIR)/boot.o $(filter-out $(BUILD_DIR)/boot.o,$(C_OBJS) $(ASM_OBJS))

# Автоматически сгенерированные зависимости (.d файлы)
DEPS = $(C_OBJS:.o=.d)

# ==============================================================================
# USER-SPACE ИСХОДНИКИ (С разделением библиотеки, crt0 и тестов)
# ==============================================================================

# 1. Библиотека libc (компилируется один раз, линкуется ко всем тестам)
USER_LIBC_SRC = $(USER_SRC_DIR)/user_libc.c
USER_LIBC_OBJ = $(BUILD_DIR)/user_libc.o

# ✅ C Runtime Startup (NASM, линкуется ПЕРВЫМ ко всем ELF)
USER_CRT0_SRC = $(USER_SRC_DIR)/crt0.asm
USER_CRT0_OBJ = $(BUILD_DIR)/crt0.o

# ✅ [ДЕНЬ 18] setjmp/longjmp (NASM, критично для TinyCC error recovery)
USER_SETJMP_SRC = $(USER_SRC_DIR)/setjmp.asm
USER_SETJMP_OBJ = $(BUILD_DIR)/setjmp.o

# ✅ [ДЕНЬ 18-19] TinyCC Adaptation Layer (qsort, bsearch, strerror, fwrite, atexit, etc.)
USER_TCC_BM_SRC = $(USER_SRC_DIR)/tcc_lib_os.c
USER_TCC_BM_OBJ = $(BUILD_DIR)/tcc_lib_os.o

# ✅ [ДЕНЬ 24] PID 1 Init Launcher (Ring 3)
INIT_SRC = $(USER_SRC_DIR)/init.c
INIT_OBJ = $(BUILD_DIR)/init.o
INIT_ELF = $(USER_SBIN_DIR)/init.elf

# ✅ [ДЕНЬ 24] Ring 3 Shell (заменяет kernel shell)
SHELL_USER_SRC = $(USER_SRC_DIR)/shell_user.c
SHELL_USER_OBJ = $(BUILD_DIR)/shell_user.o
SHELL_USER_ELF = $(USER_BIN_DIR)/shell.elf

# ✅ [ДЕНЬ 24] Список "библиотечных" и "системных" .c файлов user-space
#    Все файлы в этом списке исключаются из USER_TEST_C_SOURCES через filter-out.
#    init.c и shell_user.c — это не тесты, а системные процессы.
USER_LIB_C_SOURCES = $(USER_LIBC_SRC) $(USER_TCC_BM_SRC) $(INIT_SRC) $(SHELL_USER_SRC)

# ✅ [ДЕНЬ 18-19] Список "библиотечных" .asm файлов user-space (НЕ компилируются как ELF)
USER_LIB_ASM_SOURCES = $(USER_CRT0_SRC) $(USER_SETJMP_SRC)

# 2. ASM исходники user-space (ИСКЛЮЧАЕМ crt0.asm и setjmp.asm через filter-out)
USER_ASM_SOURCES = $(filter-out $(USER_LIB_ASM_SOURCES),$(wildcard $(USER_SRC_DIR)/*.asm))
USER_ASM_OBJS = $(patsubst $(USER_SRC_DIR)/%.asm,$(BUILD_DIR)/user_%.o,$(USER_ASM_SOURCES))

# 3. Тестовые C-исходники (ИСКЛЮЧАЕМ библиотеки и системные процессы через filter-out)
USER_TEST_C_SOURCES = $(filter-out $(USER_LIB_C_SOURCES),$(wildcard $(USER_SRC_DIR)/*.c))
USER_TEST_C_OBJS = $(patsubst $(USER_SRC_DIR)/%.c,$(BUILD_DIR)/user_%.o,$(USER_TEST_C_SOURCES))

# 4. Финальные ELF бинарники (только тесты + asm программы)
USER_ELFS = $(patsubst $(USER_SRC_DIR)/%.c,$(USER_BIN_DIR)/%.elf,$(USER_TEST_C_SOURCES)) \
            $(patsubst $(USER_SRC_DIR)/%.asm,$(USER_BIN_DIR)/%.elf,$(USER_ASM_SOURCES))

# ✅ Полная цепочка библиотечных объектов для линковки (порядок важен!)
#    crt0.o ПЕРВЫМ (_start), затем libc, затем tcc_lib_os, затем setjmp
USER_LIB_OBJS = $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_TCC_BM_OBJ) $(USER_SETJMP_OBJ)

# ✅ [ДЕНЬ 20] TinyCC Compiler (компилируем сам TinyCC в user-space ELF)
TCC_SRC = external/tcc_src/tcc.c
TCC_OBJ = $(BUILD_DIR)/tcc.o
TCC_ELF = $(USER_BIN_DIR)/tcc.elf

# Флаги компиляции TinyCC (монолитный статический бинарник)
TCC_CFLAGS = -m32 -nostdlib -static -ffreestanding -O2 -Wall -Wextra \
             -fno-optimize-sibling-calls \
             -DCONFIG_TCC_STATIC \
             -DONE_SOURCE=1 \
             -Iuser_src \
             -I$(INITRD_ROOT)/include

# ==============================================================================
# ГЛАВНЫЕ ТАРГЕТЫ
# ==============================================================================
all: user_programs iso

# Линковка ядра
$(KERNEL_BIN): $(OBJ) linker.ld | $(BUILD_DIR)
	@echo "[LINK] $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter-out linker.ld,$^)

# Компиляция C файлов ядра
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "[CC]   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Компиляция ASM файлов ядра
$(BUILD_DIR)/%.o: %.asm | $(BUILD_DIR)
	@echo "[AS]   $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# ==============================================================================
# USER-SPACE PROGRAMS (ELF Binaries)
# ==============================================================================

# Компиляция самой библиотеки user_libc (один раз для всех тестов)
$(USER_LIBC_OBJ): $(USER_LIBC_SRC) $(USER_SRC_DIR)/user_libc.h $(USER_SRC_DIR)/user_syscalls.h | $(BUILD_DIR)
	@echo "[USER LIB] $< -> $@"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

# ✅ [ДЕНЬ 18-19] Компиляция TinyCC Adaptation Layer (tcc_lib_os.c)
$(USER_TCC_BM_OBJ): $(USER_TCC_BM_SRC) $(USER_SRC_DIR)/user_libc.h | $(BUILD_DIR)
	@echo "[USER ADAPT] $< -> $@"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

# ✅ Компиляция crt0.asm через NASM
$(USER_CRT0_OBJ): $(USER_CRT0_SRC) | $(BUILD_DIR)
	@echo "[USER AS-NASM] $< -> $@"
	@$(AS) $(ASFLAGS) $< -o $@

# ✅ [ДЕНЬ 18-19] Компиляция setjmp/longjmp через NASM
$(USER_SETJMP_OBJ): $(USER_SETJMP_SRC) | $(BUILD_DIR)
	@echo "[USER AS-NASM] $< -> $@"
	@$(AS) $(ASFLAGS) $< -o $@

# ✅ [ДЕНЬ 24] Компиляция PID 1 Init Launcher
$(INIT_OBJ): $(INIT_SRC) $(USER_SRC_DIR)/user_libc.h $(USER_SRC_DIR)/user_syscalls.h | $(BUILD_DIR)
	@echo "[INIT] Compiling PID 1 Launcher..."
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

# ✅ [ДЕНЬ 24] Линковка init.elf в /sbin/
$(INIT_ELF): $(INIT_OBJ) $(USER_LIB_OBJS) | $(USER_SBIN_DIR)
	@echo "[INIT] Linking init.elf..."
	@$(USER_CC) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(INIT_OBJ) $(USER_LIBC_OBJ) $(USER_TCC_BM_OBJ) $(USER_SETJMP_OBJ)
	@echo "[ OK ] Init compiled: $@"

# ✅ [ДЕНЬ 24] Компиляция Ring 3 Shell
$(SHELL_USER_OBJ): $(SHELL_USER_SRC) $(USER_SRC_DIR)/user_libc.h $(USER_SRC_DIR)/user_syscalls.h | $(BUILD_DIR)
	@echo "[SHELL-USER] Compiling Ring 3 Shell..."
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

# ✅ [ДЕНЬ 24] Линковка shell.elf в /bin/
$(SHELL_USER_ELF): $(SHELL_USER_OBJ) $(USER_LIB_OBJS) | $(USER_BIN_DIR)
	@echo "[SHELL-USER] Linking shell.elf..."
	@$(USER_CC) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(SHELL_USER_OBJ) $(USER_LIBC_OBJ) $(USER_TCC_BM_OBJ) $(USER_SETJMP_OBJ)
	@echo "[ OK ] Shell compiled: $@"

$(USER_SBIN_DIR):
	@mkdir -p $@

# ✅ [ДЕНЬ 20] Компиляция TinyCC (tcc.c -> tcc.o)
$(TCC_OBJ): $(TCC_SRC) $(USER_SRC_DIR)/user_libc.h $(USER_SRC_DIR)/user_syscalls.h | $(BUILD_DIR)
	@echo "[TCC] Compiling TinyCC from source..."
	@$(USER_CC) $(TCC_CFLAGS) -c $(TCC_SRC) -o $@

# ✅ [ДЕНЬ 20] Компиляция libtcc1.c (runtime helpers для 64-bit math / FPU)
$(BUILD_DIR)/libtcc1.o: | $(BUILD_DIR)
	@echo "[TCC-LIB] Compiling libtcc1.c (64-bit math helpers)..."
	@if [ -f "external/tcc_src/libtcc1.c" ]; then \
		$(USER_CC) $(USER_CFLAGS) -c external/tcc_src/libtcc1.c -o $@; \
	elif [ -f "external/tcc_src/lib/libtcc1.c" ]; then \
		$(USER_CC) $(USER_CFLAGS) -c external/tcc_src/lib/libtcc1.c -o $@; \
	else \
		echo "[FATAL] libtcc1.c not found in external/tcc_src/ or external/tcc_src/lib/"; \
		exit 1; \
	fi
	
# ✅ [ДЕНЬ 20] Линковка TinyCC в финальный ELF-бинарник
$(TCC_ELF): $(TCC_OBJ) $(USER_LIB_OBJS) $(BUILD_DIR)/libtcc1.o | $(USER_BIN_DIR)
	@echo "[TCC] Linking TinyCC with libc + libtcc1..."
	@$(USER_CC) $(USER_LDFLAGS) -o $@ \
		$(USER_CRT0_OBJ) \
		$(TCC_OBJ) \
		$(USER_LIBC_OBJ) \
		$(USER_TCC_BM_OBJ) \
		$(USER_SETJMP_OBJ) \
		$(BUILD_DIR)/libtcc1.o
	@echo "[ OK ] TinyCC compiled: $@"

# ✅ [ДЕНЬ 18-19] Линковка C-тестов с ПОЛНОЙ цепочкой библиотечных объектов
$(USER_BIN_DIR)/%.elf: $(USER_SRC_DIR)/%.c $(USER_LIB_OBJS) | $(USER_BIN_DIR)
	@echo "[USER CC] $< -> $@"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/user_$*.o
	@$(USER_CC) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(BUILD_DIR)/user_$*.o $(USER_LIBC_OBJ) $(USER_TCC_BM_OBJ) $(USER_SETJMP_OBJ)

# ✅ [ДЕНЬ 18-19] Линковка ASM-тестов
$(USER_BIN_DIR)/%.elf: $(USER_SRC_DIR)/%.asm $(USER_LIB_OBJS) | $(USER_BIN_DIR)
	@echo "[USER AS] $< -> $@"
	@$(AS) -f elf32 $< -o $(BUILD_DIR)/user_$*.o
	@$(USER_CC) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(BUILD_DIR)/user_$*.o $(USER_LIBC_OBJ) $(USER_TCC_BM_OBJ) $(USER_SETJMP_OBJ)

$(USER_BIN_DIR):
	@mkdir -p $(USER_BIN_DIR)

# ✅ [ДЕНЬ 24] Включаем init.elf и shell.elf в user_programs
user_programs: $(USER_ELFS) $(TCC_ELF) $(INIT_ELF) $(SHELL_USER_ELF)
	@if [ -n "$(USER_ELFS)" ]; then \
		echo "[ OK ] Built $$(echo $(USER_ELFS) | wc -w) user-space programs"; \
	fi
	@echo "[ OK ] TinyCC, Init, and Shell compiled"

# ==============================================================================
# INITRD (АВТОМАТИЧЕСКАЯ УПАКОВКА TAR USTAR + DAY 19 TOOLCHAIN INJECTION)
# ==============================================================================
$(INITRD_TAR): $(KERNEL_BIN) user_programs | $(INITRD_ROOT)
	@echo "[INITRD] Preparing initrd root..."
	@rm -rf $(INITRD_ROOT)
	@mkdir -p $(INITRD_ROOT)
	
	@# Копируем статические файлы из initrd_src/
	@if [ -d "$(INITRD_SRC_DIR)" ]; then \
		cp -r $(INITRD_SRC_DIR)/* $(INITRD_ROOT)/ 2>/dev/null || true; \
	fi
	
	@# Копируем user-space бинарники в bin/
	@if [ -d "$(USER_BIN_DIR)" ] && [ -n "$$(ls -A $(USER_BIN_DIR) 2>/dev/null)" ]; then \
		mkdir -p $(INITRD_ROOT)/bin; \
		cp $(USER_BIN_DIR)/*.elf $(INITRD_ROOT)/bin/ 2>/dev/null || true; \
	fi

	@# ✅ [ДЕНЬ 24] Копируем init.elf в /sbin/ (PID 1 Launcher)
	@if [ -f "$(INIT_ELF)" ]; then \
		mkdir -p $(INITRD_ROOT)/sbin; \
		cp $(INIT_ELF) $(INITRD_ROOT)/sbin/init.elf; \
		echo "[INITRD] Copied init.elf to /sbin/"; \
	fi

	@# =========================================================================
	@# DAY 19: AUTOMATIC TOOLCHAIN INJECTION (TinyCC Support)
	@# =========================================================================
	@echo "[TOOLCHAIN] Injecting TinyCC support files into initrd..."
	@mkdir -p $(INITRD_ROOT)/lib
	@mkdir -p $(INITRD_ROOT)/usr/lib
	@mkdir -p $(INITRD_ROOT)/include
	@mkdir -p $(INITRD_ROOT)/usr/include

	@# 1. CRT Files (crt0, crt1, crti, crtn)
	@cp $(USER_CRT0_OBJ) $(INITRD_ROOT)/lib/crt0.o
	@cp $(USER_CRT0_OBJ) $(INITRD_ROOT)/lib/crt1.o
	@cp $(USER_CRT0_OBJ) $(INITRD_ROOT)/usr/lib/crt0.o
	@cp $(USER_CRT0_OBJ) $(INITRD_ROOT)/usr/lib/crt1.o
	
	@# crti and crtn as empty stubs
	@echo "section .text" > $(BUILD_DIR)/crti.asm
	@echo "section .text" > $(BUILD_DIR)/crtn.asm
	@$(AS) $(ASFLAGS) $(BUILD_DIR)/crti.asm -o $(INITRD_ROOT)/lib/crti.o
	@$(AS) $(ASFLAGS) $(BUILD_DIR)/crtn.asm -o $(INITRD_ROOT)/lib/crtn.o
	@cp $(INITRD_ROOT)/lib/crti.o $(INITRD_ROOT)/usr/lib/crti.o
	@cp $(INITRD_ROOT)/lib/crtn.o $(INITRD_ROOT)/usr/lib/crtn.o

	@# 2. libtcc1.a
	@if [ -f "external/tcc_src/libtcc1.c" ]; then \
		echo "[TOOLCHAIN] Compiling libtcc1.a from external/tcc_src/libtcc1.c..."; \
		$(USER_CC) $(USER_CFLAGS) -c external/tcc_src/libtcc1.c -o $(BUILD_DIR)/libtcc1.o; \
		$(AR) rcs $(INITRD_ROOT)/lib/libtcc1.a $(BUILD_DIR)/libtcc1.o; \
		cp $(INITRD_ROOT)/lib/libtcc1.a $(INITRD_ROOT)/usr/lib/libtcc1.a; \
	elif [ -f "external/tcc_src/lib/libtcc1.c" ]; then \
		echo "[TOOLCHAIN] Compiling libtcc1.a from external/tcc_src/lib/libtcc1.c..."; \
		$(USER_CC) $(USER_CFLAGS) -c external/tcc_src/lib/libtcc1.c -o $(BUILD_DIR)/libtcc1.o; \
		$(AR) rcs $(INITRD_ROOT)/lib/libtcc1.a $(BUILD_DIR)/libtcc1.o; \
		cp $(INITRD_ROOT)/lib/libtcc1.a $(INITRD_ROOT)/usr/lib/libtcc1.a; \
	else \
		echo "[TOOLCHAIN] WARNING: libtcc1.c not found. TinyCC may fail to link 64-bit math."; \
	fi

	@# 3. Fake POSIX Headers
	@echo "[TOOLCHAIN] Generating fake POSIX headers..."
	@for hdr in stdio.h stdlib.h string.h errno.h fcntl.h unistd.h time.h setjmp.h signal.h stdarg.h stddef.h stdint.h stdbool.h limits.h float.h math.h; do \
		echo "#include \"user_libc.h\"" > $(INITRD_ROOT)/include/$$hdr; \
		echo "#include \"user_libc.h\"" > $(INITRD_ROOT)/usr/include/$$hdr; \
	done
	@mkdir -p $(INITRD_ROOT)/include/sys
	@mkdir -p $(INITRD_ROOT)/usr/include/sys
	@for hdr in time.h mman.h types.h stat.h; do \
		echo "#include \"../user_libc.h\"" > $(INITRD_ROOT)/include/sys/$$hdr; \
		echo "#include \"../user_libc.h\"" > $(INITRD_ROOT)/usr/include/sys/$$hdr; \
	done
	
	@# Copy user_libc.h and user_syscalls.h to include paths
	@cp $(USER_SRC_DIR)/user_libc.h $(INITRD_ROOT)/include/
	@cp $(USER_SRC_DIR)/user_libc.h $(INITRD_ROOT)/usr/include/
	@cp $(USER_SRC_DIR)/user_syscalls.h $(INITRD_ROOT)/include/
	@cp $(USER_SRC_DIR)/user_syscalls.h $(INITRD_ROOT)/usr/include/

	@echo "[TAR]  Packing initrd.tar (UStar format)..."
	@rm -f $(INITRD_TAR)
	@(cd $(INITRD_ROOT) && tar --format=ustar -cf ../initrd.tar .)
	@echo "[ OK ] initrd.tar created"

$(INITRD_ROOT):
	@mkdir -p $(INITRD_ROOT)

# ==============================================================================
# ПРОВЕРКИ И ПОДГОТОВКА ПЕРЕД СБОРКОЙ ISO
# ==============================================================================
check_prerequisites:
	@echo "[CHECK] Verifying prerequisites..."
	@if [ ! -f "linker.ld" ]; then \
		echo "[FATAL] linker.ld not found!"; \
		exit 1; \
	fi
	@echo "[ OK ] All prerequisites met."

prepare_iso_dir: $(KERNEL_BIN) $(INITRD_TAR)
	@echo "[ISO]  Preparing ISO directory structure..."
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL_BIN) $(ISO_DIR)/boot/kernel.bin
	@cp $(INITRD_TAR) $(ISO_DIR)/boot/initrd.tar
	
	@echo "[GRUB] Generating grub.cfg..."
	@echo 'set timeout=0' > $(ISO_DIR)/boot/grub/grub.cfg
	@echo 'set default=0' >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '' >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo 'menuentry "Enclave Operating System" {' >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '    multiboot /boot/kernel.bin' >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '    module /boot/initrd.tar' >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '    boot' >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '}' >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "[ OK ] ISO directory prepared"

# ==============================================================================
# СБОРКА ISO (GRUB-MKRESCUE)
# ==============================================================================
iso: user_programs check_prerequisites prepare_iso_dir
	@echo "[ISO]  Generating bootable ISO with grub-mkrescue..."
	@grub-mkrescue -o $(ISO_NAME) $(ISO_DIR) 2>/dev/null
	@echo "[ OK ] ISO created: $(ISO_NAME)"

# ==============================================================================
# ATA DISK (для тестирования FAT32)
# ==============================================================================
create_disk:
	@echo "[DISK] Создаем FAT32 диск (100 MB)..."
	@qemu-img create -f raw $(DISK_IMG) 100M
	@echo "[ OK ] $(DISK_IMG) создан"

# ==============================================================================
# ЗАПУСК (QEMU)
# ==============================================================================
run: iso
	@echo "[QEMU] Стартуем $(ISO_NAME)..."
	@qemu-system-i386 -cdrom $(ISO_NAME) -m 1024M -serial stdio -no-reboot

# Запуск с ATA диском
run_ata: iso create_disk
	@echo "[QEMU] Стартуем с ATA диском..."
	@qemu-system-i386 -cdrom $(ISO_NAME) -m 1024M -serial stdio -no-reboot \
		-boot d \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,media=disk

# ==============================================================================
# ОЧИСТКА
# ==============================================================================
clean:
	@echo "[CLEAN] Удаляю build/..."
	@rm -rf $(BUILD_DIR)
	@echo "[ OK ] Очистка завершена"

# ==============================================================================
# ВСПОМОГАТЕЛЬНЫЕ ТАРГЕТЫ
# ==============================================================================
.PHONY: all clean run run_ata create_disk iso check_prerequisites prepare_iso_dir user_programs

# Включаем автоматически сгенерированные зависимости
-include $(DEPS)