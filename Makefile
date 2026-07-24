# ==============================================================================
# ENCLAVE OPERATING SYSTEM — Makefile (Single Source of Truth)
# 
#
# Changes:
#   - tcc_lib_os.c удалён из сборки
#   - libc.a = user_libc.o + setjmp.o
#   - Хост НЕ собирает тесты из user_src
#   - Тесты копируются исходниками из initrd_src/ и компилируются внутри ОС
#   - Toolchain headers генерируются отдельно в build/toolchain/include
#   - libtcc1.a собирается один раз
#   - initrd.tar содержит /lib/libc.a, /lib/libtcc1.a, CRT и fake headers
# ==============================================================================

# ==============================================================================
# КОНФИГУРАЦИЯ И ИНСТРУМЕНТЫ
# ==============================================================================

CC       = i686-linux-gnu-gcc
AS       = nasm
USER_CC  = $(CC)
AR       = i686-linux-gnu-ar

# Директории
BUILD_DIR        = build
ISO_DIR          = $(BUILD_DIR)/isodir
INITRD_ROOT      = $(BUILD_DIR)/initrd_root
TOOLCHAIN_DIR    = $(BUILD_DIR)/toolchain
TOOLCHAIN_INC    = $(TOOLCHAIN_DIR)/include
TOOLCHAIN_STAMP  = $(TOOLCHAIN_INC)/.headers.stamp
USER_BIN_DIR     = $(BUILD_DIR)/bin
USER_SBIN_DIR    = $(BUILD_DIR)/sbin
USER_SRC_DIR     = user_src
INITRD_SRC_DIR   = initrd_src

# Имена файлов
ISO_NAME     = $(BUILD_DIR)/enclave_os.iso
KERNEL_BIN   = $(BUILD_DIR)/kernel.bin
INITRD_TAR   = $(BUILD_DIR)/initrd.tar
DISK_IMG     = $(BUILD_DIR)/disk.img

# ==============================================================================
# ФЛАГИ КОМПИЛЯЦИИ ЯДРА
# ==============================================================================

CFLAGS  = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
CFLAGS += -fno-pie -fno-pic -fno-stack-protector
CFLAGS += -mno-sse -mno-sse2 -mno-mmx -mno-3dnow
CFLAGS += -mincoming-stack-boundary=2 -g
CFLAGS += -MMD -MP

KERNEL_LDFLAGS = -T linker.ld -nostdlib -no-pie
LDLIBS         = -lgcc

ASFLAGS = -f elf32 -g

# ==============================================================================
# ФЛАГИ КОМПИЛЯЦИИ USER-SPACE
# ==============================================================================

USER_CFLAGS  = -m32 -nostdlib -static -ffreestanding -O2 -Wall -Wextra
USER_CFLAGS += -fno-optimize-sibling-calls
USER_CFLAGS += -fno-pie -fno-pic -fno-stack-protector
USER_CFLAGS += -I$(USER_SRC_DIR) -I$(TOOLCHAIN_INC)
USER_CFLAGS += -MMD -MP

USER_LDFLAGS = -nostdlib -static -no-pie -T $(USER_SRC_DIR)/user_linker.ld
USER_LDLIBS  = -lgcc

# ==============================================================================
# ФЛАГИ КОМПИЛЯЦИИ TINYCC
# ==============================================================================

TCC_CFLAGS  = -m32 -nostdlib -static -ffreestanding -O2 -Wall -Wextra
TCC_CFLAGS += -fno-optimize-sibling-calls
TCC_CFLAGS += -fno-pie -fno-pic -fno-stack-protector
TCC_CFLAGS += -DCONFIG_TCC_STATIC
TCC_CFLAGS += -DONE_SOURCE=1
TCC_CFLAGS += -I$(USER_SRC_DIR) -I$(TOOLCHAIN_INC)
TCC_CFLAGS += -MMD -MP

# ==============================================================================
# FAKE POSIX HEADERS ДЛЯ TCC И ВНУТРИОС-КОМПИЛЯЦИИ
# ==============================================================================

FAKE_HEADERS = \
	stdio.h \
	stdlib.h \
	string.h \
	strings.h \
	errno.h \
	fcntl.h \
	unistd.h \
	time.h \
	setjmp.h \
	signal.h \
	stdarg.h \
	stddef.h \
	stdint.h \
	stdbool.h \
	inttypes.h \
	limits.h \
	float.h \
	math.h \
	ctype.h \
	dirent.h \
	alloca.h \
	malloc.h \
	endian.h \
	features.h

FAKE_SYS_HEADERS = \
	time.h \
	mman.h \
	types.h \
	stat.h \
	wait.h \
	ioctl.h \
	utsname.h \
	sysinfo.h \
	dirent.h \
	file.h \
	param.h \
	resource.h \
	cdefs.h

# ==============================================================================
# ИСХОДНИКИ И ОБЪЕКТЫ ЯДРА
# ==============================================================================

C_SOURCES   = $(wildcard *.c)
ASM_SOURCES = $(wildcard *.asm)

# test_runner.c больше не собирается ядром
KERNEL_EXCLUDE = test_runner.c

C_SOURCES_FILTERED = $(filter-out $(KERNEL_EXCLUDE),$(C_SOURCES))

C_OBJS   = $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES_FILTERED))
ASM_OBJS = $(patsubst %.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

# boot.o ОБЯЗАТЕЛЬНО первым: Multiboot header должен быть в первых 8KB
OBJ = $(BUILD_DIR)/boot.o $(filter-out $(BUILD_DIR)/boot.o,$(C_OBJS) $(ASM_OBJS))

# ==============================================================================
# USER-SPACE ОБЪЕКТЫ
# ==============================================================================

USER_HEADERS = \
	$(USER_SRC_DIR)/user_libc.h \
	$(USER_SRC_DIR)/user_syscalls.h

USER_LIBC_SRC = $(USER_SRC_DIR)/user_libc.c
USER_LIBC_OBJ = $(BUILD_DIR)/user_libc.o

USER_CRT0_SRC = $(USER_SRC_DIR)/crt0.asm
USER_CRT0_OBJ = $(BUILD_DIR)/crt0.o

USER_SETJMP_SRC = $(USER_SRC_DIR)/setjmp.asm
USER_SETJMP_OBJ = $(BUILD_DIR)/setjmp.o

USER_LIB_A = $(BUILD_DIR)/libc.a

# ==============================================================================
# СИСТЕМНЫЕ RING 3 ПРОГРАММЫ
# ==============================================================================

INIT_SRC = $(USER_SRC_DIR)/init.c
INIT_OBJ = $(BUILD_DIR)/init.o
INIT_ELF = $(USER_SBIN_DIR)/init.elf

SHELL_USER_SRC = $(USER_SRC_DIR)/shell_user.c
SHELL_USER_OBJ = $(BUILD_DIR)/shell_user.o
SHELL_USER_ELF = $(USER_BIN_DIR)/shell.elf

# ==============================================================================
# TINYCC
# ==============================================================================

TCC_SRC = external/tcc_src/tcc.c
TCC_OBJ = $(BUILD_DIR)/tcc.o
TCC_ELF = $(USER_BIN_DIR)/tcc.elf

LIBTCC1_SRC := $(firstword $(wildcard external/tcc_src/libtcc1.c external/tcc_src/lib/libtcc1.c))
LIBTCC1_OBJ = $(BUILD_DIR)/libtcc1.o
LIBTCC1_A   = $(BUILD_DIR)/libtcc1.a

# ==============================================================================
# CRT STUBS ДЛЯ TCC
# ==============================================================================

CRTI_OBJ = $(BUILD_DIR)/crti.o
CRTN_OBJ = $(BUILD_DIR)/crtn.o

# ==============================================================================
# INITRD SOURCE FILES
# ==============================================================================

# Нужно, чтобы initrd.tar пересобирался при изменении initrd_src/examples/stres.c
INITRD_SRC_FILES := $(shell find $(INITRD_SRC_DIR) -type f 2>/dev/null)

# ==============================================================================
# ПРОГРАММЫ, КОТОРЫЕ РЕАЛЬНО СОБИРАЮТСЯ НА ХОСТЕ
# ==============================================================================

USER_PROGRAMS = \
	$(INIT_ELF) \
	$(SHELL_USER_ELF) \
	$(TCC_ELF)

# ==============================================================================
# ГЛАВНЫЕ ТАРГЕТЫ
# ==============================================================================

all: iso

# ==============================================================================
# ЯДРО
# ==============================================================================

$(KERNEL_BIN): $(OBJ) linker.ld | $(BUILD_DIR)
	@echo "[LINK] $@"
	@$(CC) $(CFLAGS) $(KERNEL_LDFLAGS) -o $@ $(filter-out linker.ld,$^) $(LDLIBS)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "[CC]   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.asm | $(BUILD_DIR)
	@echo "[AS]   $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

# ==============================================================================
# TOOLCHAIN HEADERS
# ==============================================================================

$(TOOLCHAIN_DIR):
	@mkdir -p $@

$(TOOLCHAIN_STAMP): $(USER_HEADERS) | $(TOOLCHAIN_DIR)
	@echo "[TOOLCHAIN] Generating fake POSIX headers..."
	@rm -rf $(TOOLCHAIN_INC)
	@mkdir -p $(TOOLCHAIN_INC)/sys
	@for h in $(FAKE_HEADERS); do \
		echo '#include "user_libc.h"' > $(TOOLCHAIN_INC)/$$h; \
	done
	@for h in $(FAKE_SYS_HEADERS); do \
		echo '#include "../user_libc.h"' > $(TOOLCHAIN_INC)/sys/$$h; \
	done
	@cp $(USER_SRC_DIR)/user_libc.h $(TOOLCHAIN_INC)/
	@cp $(USER_SRC_DIR)/user_syscalls.h $(TOOLCHAIN_INC)/
	@if [ -f $(USER_SRC_DIR)/config.h ]; then \
		cp $(USER_SRC_DIR)/config.h $(TOOLCHAIN_INC)/; \
	fi
	@touch $@

# ==============================================================================
# USER LIBC
# ==============================================================================

$(USER_LIBC_OBJ): $(USER_LIBC_SRC) $(USER_HEADERS) $(TOOLCHAIN_STAMP) | $(BUILD_DIR)
	@echo "[USER LIB] $< -> $@"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(USER_CRT0_OBJ): $(USER_CRT0_SRC) | $(BUILD_DIR)
	@echo "[USER AS] $< -> $@"
	@$(AS) $(ASFLAGS) $< -o $@

$(USER_SETJMP_OBJ): $(USER_SETJMP_SRC) | $(BUILD_DIR)
	@echo "[USER AS] $< -> $@"
	@$(AS) $(ASFLAGS) $< -o $@

$(USER_LIB_A): $(USER_LIBC_OBJ) $(USER_SETJMP_OBJ) | $(BUILD_DIR)
	@echo "[AR]   $@"
	@rm -f $@
	@$(AR) rcs $@ $(USER_LIBC_OBJ) $(USER_SETJMP_OBJ)

# ==============================================================================
# INIT
# ==============================================================================

$(INIT_OBJ): $(INIT_SRC) $(USER_HEADERS) $(TOOLCHAIN_STAMP) | $(BUILD_DIR)
	@echo "[INIT] $< -> $@"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(INIT_ELF): $(INIT_OBJ) $(USER_CRT0_OBJ) $(USER_LIB_A) $(USER_SRC_DIR)/user_linker.ld | $(USER_SBIN_DIR)
	@echo "[INIT] Linking init.elf..."
	@$(USER_CC) $(USER_LDFLAGS) -o $@ \
		$(USER_CRT0_OBJ) \
		$(INIT_OBJ) \
		-Wl,--start-group $(USER_LIB_A) -Wl,--end-group \
		$(USER_LDLIBS)
	@echo "[ OK ] Init compiled: $@"

$(USER_SBIN_DIR):
	@mkdir -p $@

# ==============================================================================
# SHELL
# ==============================================================================

$(SHELL_USER_OBJ): $(SHELL_USER_SRC) $(USER_HEADERS) $(TOOLCHAIN_STAMP) | $(BUILD_DIR)
	@echo "[SHELL] $< -> $@"
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(SHELL_USER_ELF): $(SHELL_USER_OBJ) $(USER_CRT0_OBJ) $(USER_LIB_A) $(USER_SRC_DIR)/user_linker.ld | $(USER_BIN_DIR)
	@echo "[SHELL] Linking shell.elf..."
	@$(USER_CC) $(USER_LDFLAGS) -o $@ \
		$(USER_CRT0_OBJ) \
		$(SHELL_USER_OBJ) \
		-Wl,--start-group $(USER_LIB_A) -Wl,--end-group \
		$(USER_LDLIBS)
	@echo "[ OK ] Shell compiled: $@"

$(USER_BIN_DIR):
	@mkdir -p $@

# ==============================================================================
# TINYCC BUILD
# ==============================================================================

ifneq ($(wildcard $(TCC_SRC)),)

$(TCC_OBJ): $(TCC_SRC) $(USER_HEADERS) $(TOOLCHAIN_STAMP) | $(BUILD_DIR)
	@echo "[TCC] Compiling TinyCC from source..."
	@$(USER_CC) $(TCC_CFLAGS) -c $(TCC_SRC) -o $@

$(TCC_ELF): $(TCC_OBJ) $(USER_CRT0_OBJ) $(USER_LIB_A) $(LIBTCC1_A) $(USER_SRC_DIR)/user_linker.ld | $(USER_BIN_DIR)
	@echo "[TCC] Linking TinyCC..."
	@$(USER_CC) $(USER_LDFLAGS) -o $@ \
		$(USER_CRT0_OBJ) \
		$(TCC_OBJ) \
		-Wl,--start-group $(USER_LIB_A) $(LIBTCC1_A) -Wl,--end-group \
		$(USER_LDLIBS)
	@echo "[ OK ] TinyCC compiled: $@"

else

$(TCC_ELF):
	@echo "[FATAL] $(TCC_SRC) not found. TinyCC is required for self-hosting."
	@exit 1

endif

# ==============================================================================
# LIBTCC1
# ==============================================================================

ifeq ($(LIBTCC1_SRC),)

$(warning libtcc1.c not found. Creating empty libtcc1.a. 64-bit math helpers may be missing.)

$(LIBTCC1_A): | $(BUILD_DIR)
	@echo "[TCC-LIB] WARNING: libtcc1.c not found; creating stub libtcc1.a"
	@rm -f $@
	@echo "section .text" > $(BUILD_DIR)/libtcc1_stub.asm
	@$(AS) $(ASFLAGS) $(BUILD_DIR)/libtcc1_stub.asm -o $(BUILD_DIR)/libtcc1_stub.o
	@$(AR) rcs $@ $(BUILD_DIR)/libtcc1_stub.o

else

$(LIBTCC1_OBJ): $(LIBTCC1_SRC) $(TOOLCHAIN_STAMP) | $(BUILD_DIR)
	@echo "[TCC-LIB] Compiling libtcc1.c..."
	@$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(LIBTCC1_A): $(LIBTCC1_OBJ) | $(BUILD_DIR)
	@echo "[TCC-LIB] Archiving libtcc1.a..."
	@rm -f $@
	@$(AR) rcs $@ $(LIBTCC1_OBJ)

endif

# ==============================================================================
# CRT STUBS
# ==============================================================================

$(CRTI_OBJ): | $(BUILD_DIR)
	@echo "[CRT] Creating crti.o stub"
	@echo "section .text" > $(BUILD_DIR)/crti.asm
	@$(AS) $(ASFLAGS) $(BUILD_DIR)/crti.asm -o $@

$(CRTN_OBJ): | $(BUILD_DIR)
	@echo "[CRT] Creating crtn.o stub"
	@echo "section .text" > $(BUILD_DIR)/crtn.asm
	@$(AS) $(ASFLAGS) $(BUILD_DIR)/crtn.asm -o $@

# ==============================================================================
# USER PROGRAMS TARGET
# ==============================================================================

user_programs: $(USER_PROGRAMS)
	@echo "[ OK ] Built host-side Ring 3 system programs: init, shell, tcc"
	@echo "[ OK ] Tests are NOT built on host. Use TinyCC inside Enclave OS."

# ==============================================================================
# INITRD
# ==============================================================================

$(INITRD_ROOT):
	@mkdir -p $@

$(INITRD_TAR): \
	$(KERNEL_BIN) \
	$(USER_PROGRAMS) \
	$(USER_LIB_A) \
	$(LIBTCC1_A) \
	$(TOOLCHAIN_STAMP) \
	$(CRTI_OBJ) \
	$(CRTN_OBJ) \
	$(INITRD_SRC_FILES) \
	| $(INITRD_ROOT)
	@echo "[INITRD] Preparing initrd root..."
	@rm -rf $(INITRD_ROOT)
	@mkdir -p $(INITRD_ROOT)/bin
	@mkdir -p $(INITRD_ROOT)/sbin
	@mkdir -p $(INITRD_ROOT)/lib
	@mkdir -p $(INITRD_ROOT)/usr/lib
	@mkdir -p $(INITRD_ROOT)/include
	@mkdir -p $(INITRD_ROOT)/usr/include

	@# Копируем исходники из initrd_src/, включая examples/stres.c
	@if [ -d "$(INITRD_SRC_DIR)" ]; then \
		cp -r $(INITRD_SRC_DIR)/. $(INITRD_ROOT)/; \
	fi

	@# Копируем Ring 3 бинарники
	@if [ -d "$(USER_BIN_DIR)" ] && [ -n "$$(ls -A $(USER_BIN_DIR) 2>/dev/null)" ]; then \
		cp $(USER_BIN_DIR)/*.elf $(INITRD_ROOT)/bin/ 2>/dev/null || true; \
	fi

	@# PID 1
	@cp $(INIT_ELF) $(INITRD_ROOT)/sbin/init.elf

	@# CRT objects
	@cp $(USER_CRT0_OBJ) $(INITRD_ROOT)/lib/crt0.o
	@cp $(USER_CRT0_OBJ) $(INITRD_ROOT)/lib/crt1.o
	@cp $(USER_CRT0_OBJ) $(INITRD_ROOT)/usr/lib/crt0.o
	@cp $(USER_CRT0_OBJ) $(INITRD_ROOT)/usr/lib/crt1.o

	@cp $(CRTI_OBJ) $(INITRD_ROOT)/lib/crti.o
	@cp $(CRTN_OBJ) $(INITRD_ROOT)/lib/crtn.o
	@cp $(CRTI_OBJ) $(INITRD_ROOT)/usr/lib/crti.o
	@cp $(CRTN_OBJ) $(INITRD_ROOT)/usr/lib/crtn.o

	@# Enclave libc.a
	@echo "[TOOLCHAIN] Installing libc.a = user_libc.o + setjmp.o"
	@cp $(USER_LIB_A) $(INITRD_ROOT)/lib/libc.a
	@cp $(USER_LIB_A) $(INITRD_ROOT)/usr/lib/libc.a

	@# TinyCC runtime library
	@cp $(LIBTCC1_A) $(INITRD_ROOT)/lib/libtcc1.a
	@cp $(LIBTCC1_A) $(INITRD_ROOT)/usr/lib/libtcc1.a

	@# Fake POSIX headers
	@cp -r $(TOOLCHAIN_INC)/. $(INITRD_ROOT)/include/
	@cp -r $(TOOLCHAIN_INC)/. $(INITRD_ROOT)/usr/include/

	@echo "[TAR]  Packing initrd.tar (UStar format)..."
	@rm -f $(INITRD_TAR)
	@tar --format=ustar -C $(INITRD_ROOT) -cf $(INITRD_TAR) .
	@echo "[ OK ] initrd.tar created"

# ==============================================================================
# ПРОВЕРКИ И ISO
# ==============================================================================

check_prerequisites:
	@echo "[CHECK] Verifying prerequisites..."
	@test -f linker.ld || { echo "[FATAL] linker.ld not found"; exit 1; }
	@test -f $(USER_SRC_DIR)/user_linker.ld || { echo "[FATAL] $(USER_SRC_DIR)/user_linker.ld not found"; exit 1; }
	@test -f $(USER_LIBC_SRC) || { echo "[FATAL] $(USER_LIBC_SRC) not found"; exit 1; }
	@test -f $(USER_SRC_DIR)/user_libc.h || { echo "[FATAL] user_libc.h not found"; exit 1; }
	@test -f $(USER_SRC_DIR)/user_syscalls.h || { echo "[FATAL] user_syscalls.h not found"; exit 1; }
	@test -f $(USER_CRT0_SRC) || { echo "[FATAL] $(USER_CRT0_SRC) not found"; exit 1; }
	@test -f $(USER_SETJMP_SRC) || { echo "[FATAL] $(USER_SETJMP_SRC) not found"; exit 1; }
	@test -f $(INIT_SRC) || { echo "[FATAL] $(INIT_SRC) not found"; exit 1; }
	@test -f $(SHELL_USER_SRC) || { echo "[FATAL] $(SHELL_USER_SRC) not found"; exit 1; }
	@echo "[ OK ] All prerequisites met."

prepare_iso_dir: $(KERNEL_BIN) $(INITRD_TAR) | check_prerequisites
	@echo "[ISO]  Preparing ISO directory structure..."
	@rm -rf $(ISO_DIR)
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

iso: prepare_iso_dir
	@echo "[ISO]  Generating bootable ISO with grub-mkrescue..."
	@grub-mkrescue -o $(ISO_NAME) $(ISO_DIR) 2>/dev/null
	@echo "[ OK ] ISO created: $(ISO_NAME)"

# ==============================================================================
# ATA DISK
# ==============================================================================

create_disk:
	@echo "[DISK] Creating FAT32 disk image (100 MB)..."
	@qemu-img create -f raw $(DISK_IMG) 100M
	@echo "[ OK ] $(DISK_IMG) created"

# ==============================================================================
# RUN
# ==============================================================================

run: iso
	@echo "[QEMU] Booting $(ISO_NAME)..."
	@qemu-system-i386 -cdrom $(ISO_NAME) -m 1024M -serial stdio -no-reboot

run_ata: iso create_disk
	@echo "[QEMU] Booting with ATA disk..."
	@qemu-system-i386 -cdrom $(ISO_NAME) -m 1024M -serial stdio -no-reboot \
		-boot d \
		-drive file=$(DISK_IMG),format=raw,if=ide,index=0,media=disk

# ==============================================================================
# CLEAN
# ==============================================================================

clean:
	@echo "[CLEAN] Removing build/..."
	@rm -rf $(BUILD_DIR)
	@echo "[ OK ] Clean complete"

# ==============================================================================
# MISC
# ==============================================================================

.PHONY: all clean run run_ata create_disk iso check_prerequisites prepare_iso_dir user_programs

.DELETE_ON_ERROR:

SUFFIXES:

# Автоматические зависимости
DEPS = \
	$(C_OBJS:.o=.d) \
	$(USER_LIBC_OBJ:.o=.d) \
	$(INIT_OBJ:.o=.d) \
	$(SHELL_USER_OBJ:.o=.d) \
	$(TCC_OBJ:.o=.d) \
	$(LIBTCC1_OBJ:.o=.d)

-include $(DEPS)
