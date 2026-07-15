# ==============================================================================
# BARE METAL OS — Makefile (Single Source of Truth)
# Версия: Day 18+ (TinyCC Adaptation Layer, setjmp/longjmp, tcc_baremetal)
# ==============================================================================

# ==============================================================================
# КОНФИГУРАЦИЯ И ИНСТРУМЕНТЫ
# ==============================================================================
CC = i686-linux-gnu-gcc
AS = nasm
USER_CC = i686-linux-gnu-gcc

# Директории
BUILD_DIR = build
ISO_DIR = $(BUILD_DIR)/isodir
INITRD_ROOT = $(BUILD_DIR)/initrd_root
USER_BIN_DIR = $(BUILD_DIR)/bin
USER_SRC_DIR = user_src
INITRD_SRC_DIR = initrd_src

# Имена файлов
ISO_NAME = $(BUILD_DIR)/metal_os.iso
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
# ✅ -fno-optimize-sibling-calls для отключения TCO (корректная рекурсия в тестах)
USER_CFLAGS = -m32 -nostdlib -static -ffreestanding -O2 -Wall -Wextra -fno-optimize-sibling-calls -Iuser_src
USER_LDFLAGS = -nostdlib -static -no-pie -T $(USER_SRC_DIR)/user_linker.ld

# ==============================================================================
# ИСХОДНИКИ И ОБЪЕКТЫ ЯДРА
# ==============================================================================
C_SOURCES = $(wildcard *.c)
ASM_SOURCES = $(wildcard *.asm)
C_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
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

# ✅ [ДЕНЬ 18] TinyCC Adaptation Layer (qsort, bsearch, strerror, isatty, etc.)
USER_TCC_BM_SRC = $(USER_SRC_DIR)/tcc_lib_os.c
USER_TCC_BM_OBJ = $(BUILD_DIR)/tcc_lib_os.o

# ✅ [ДЕНЬ 18] Список "библиотечных" .c файлов user-space (НЕ компилируются как ELF)
#    Все файлы в этом списке исключаются из USER_TEST_C_SOURCES через filter-out.
USER_LIB_C_SOURCES = $(USER_LIBC_SRC) $(USER_TCC_BM_SRC)

# ✅ [ДЕНЬ 18] Список "библиотечных" .asm файлов user-space (НЕ компилируются как ELF)
USER_LIB_ASM_SOURCES = $(USER_CRT0_SRC) $(USER_SETJMP_SRC)

# 2. ASM исходники user-space (ИСКЛЮЧАЕМ crt0.asm и setjmp.asm через filter-out)
#    🛡️ ВАЖНО: без filter-out они попали бы в USER_ELFS как отдельные бинарники!
USER_ASM_SOURCES = $(filter-out $(USER_LIB_ASM_SOURCES),$(wildcard $(USER_SRC_DIR)/*.asm))
USER_ASM_OBJS = $(patsubst $(USER_SRC_DIR)/%.asm,$(BUILD_DIR)/user_%.o,$(USER_ASM_SOURCES))

# 3. Тестовые C-исходники (ИСКЛЮЧАЕМ user_libc.c и tcc_lib_os.c через filter-out)
USER_TEST_C_SOURCES = $(filter-out $(USER_LIB_C_SOURCES),$(wildcard $(USER_SRC_DIR)/*.c))
USER_TEST_C_OBJS = $(patsubst $(USER_SRC_DIR)/%.c,$(BUILD_DIR)/user_%.o,$(USER_TEST_C_SOURCES))

# 4. Финальные ELF бинарники (только тесты + asm программы, БЕЗ crt0.elf и setjmp.elf)
USER_ELFS = $(patsubst $(USER_SRC_DIR)/%.c,$(USER_BIN_DIR)/%.elf,$(USER_TEST_C_SOURCES)) \
            $(patsubst $(USER_SRC_DIR)/%.asm,$(USER_BIN_DIR)/%.elf,$(USER_ASM_SOURCES))

# ✅ [ДЕНЬ 18] Полная цепочка библиотечных объектов для линковки (порядок важен!)
#    crt0.o ПЕРВЫМ (_start), затем тест, затем libc, затем tcc_lib_os, затем setjmp
USER_LIB_OBJS = $(USER_CRT0_OBJ) $(USER_LIBC_OBJ) $(USER_TCC_BM_OBJ) $(USER_SETJMP_OBJ)

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

# ✅ [ДЕНЬ 18] Компиляция TinyCC Adaptation Layer
$(USER_TCC_BM_OBJ): $(USER_TCC_BM_SRC) $(USER_SRC_DIR)/user_libc.h | $(BUILD_DIR)
	@echo "[USER ADAPT] $< -> $@"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

# ✅ Компиляция crt0.asm через NASM (Intel Syntax, консистентность с остальным ASM)
$(USER_CRT0_OBJ): $(USER_CRT0_SRC) | $(BUILD_DIR)
	@echo "[USER AS-NASM] $< -> $@"
	@$(AS) $(ASFLAGS) $< -o $@

# ✅ [ДЕНЬ 18] Компиляция setjmp/longjmp через NASM
$(USER_SETJMP_OBJ): $(USER_SETJMP_SRC) | $(BUILD_DIR)
	@echo "[USER AS-NASM] $< -> $@"
	@$(AS) $(ASFLAGS) $< -o $@

# ✅ [ДЕНЬ 18] Линковка C-тестов с ПОЛНОЙ цепочкой библиотечных объектов
#    Порядок: crt0.o → test.o → user_libc.o → tcc_baremetal.o → setjmp.o
$(USER_BIN_DIR)/%.elf: $(USER_SRC_DIR)/%.c $(USER_LIB_OBJS) | $(USER_BIN_DIR)
	@echo "[USER CC] $< -> $@"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/user_$*.o
	@$(USER_CC) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(BUILD_DIR)/user_$*.o $(USER_LIBC_OBJ) $(USER_TCC_BM_OBJ) $(USER_SETJMP_OBJ)

# ✅ [ДЕНЬ 18] Линковка ASM-тестов (тоже линкуем с полной цепочкой)
$(USER_BIN_DIR)/%.elf: $(USER_SRC_DIR)/%.asm $(USER_LIB_OBJS) | $(USER_BIN_DIR)
	@echo "[USER AS] $< -> $@"
	@$(AS) -f elf32 $< -o $(BUILD_DIR)/user_$*.o
	@$(USER_CC) $(USER_LDFLAGS) -o $@ $(USER_CRT0_OBJ) $(BUILD_DIR)/user_$*.o $(USER_LIBC_OBJ) $(USER_TCC_BM_OBJ) $(USER_SETJMP_OBJ)

$(USER_BIN_DIR):
	@mkdir -p $(USER_BIN_DIR)

user_programs: $(USER_ELFS)
	@if [ -n "$(USER_ELFS)" ]; then \
		echo "[ OK ] Built $$(echo $(USER_ELFS) | wc -w) user-space programs"; \
	fi

# ==============================================================================
# INITRD (АВТОМАТИЧЕСКАЯ УПАКОВКА TAR USTAR)
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
	
	@# Генерируем grub.cfg автоматически
	@echo "[GRUB] Generating grub.cfg..."
	@echo 'set timeout=0' > $(ISO_DIR)/boot/grub/grub.cfg
	@echo 'set default=0' >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '' >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo 'menuentry "Bare Metal OS" {' >> $(ISO_DIR)/boot/grub/grub.cfg
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