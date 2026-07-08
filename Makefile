# ==============================================================================
# КОНФИГУРАЦИЯ И ИНСТРУМЕНТЫ
# ==============================================================================
CC = i686-linux-gnu-gcc
AS = nasm

# Директории и имена
BUILD_DIR = build
ISO_DIR = isodir
ISO_NAME = $(BUILD_DIR)/metal_os.iso
KERNEL_BIN = $(BUILD_DIR)/metal_os.bin

# Initrd (RAM Disk)
INITRD_DIR = initrd_src
INITRD_TAR = $(ISO_DIR)/boot/initrd.tar

# Флаги компиляции (Строго по Базе Знаний: отключаем SSE, PIE, Stack Protector)
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
CFLAGS += -fno-pie -fno-pic -fno-stack-protector
CFLAGS += -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -mincoming-stack-boundary=2 -g

ASFLAGS = -f elf32 -g
LDFLAGS = -T linker.ld -nostdlib -no-pie -lgcc

# ==============================================================================
# ИСХОДНИКИ И ОБЪЕКТЫ
# ==============================================================================
# wildcard автоматически подхватит vfs.c, initrd.c, task.c и все ASM-файлы
C_SOURCES = $(wildcard *.c)
ASM_SOURCES = $(wildcard *.asm)
C_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJS = $(patsubst %.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

# boot.o ОБЯЗАТЕЛЬНО первым (Multiboot header должен быть в первых 8KB)
OBJ = $(BUILD_DIR)/boot.o $(filter-out $(BUILD_DIR)/boot.o,$(C_OBJS) $(ASM_OBJS))

# ==============================================================================
# ГЛАВНЫЕ ТАРГЕТЫ
# ==============================================================================
all: iso

# Линковка ядра
$(KERNEL_BIN): $(OBJ)
	@echo "[LINK] $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Компиляция C файлов
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "[CC]   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Компиляция ASM файлов
$(BUILD_DIR)/%.o: %.asm | $(BUILD_DIR)
	@echo "[AS]   $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# ==============================================================================
# INITRD (АВТОМАТИЧЕСКАЯ УПАКОВКА TAR USTAR)
# ==============================================================================
$(INITRD_TAR): $(wildcard $(INITRD_DIR)/* $(INITRD_DIR)/*/* $(INITRD_DIR)/*/*/*)
	@echo "[TAR]  Packing initrd.tar (UStar format)..."
	@mkdir -p $(ISO_DIR)/boot
	@rm -f $(INITRD_TAR)
	@if [ -d "$(INITRD_DIR)" ]; then \
		(cd $(INITRD_DIR) && tar --format=ustar -cf ../$(INITRD_TAR) .) || { echo "[FATAL] tar command failed!"; exit 1; }; \
	else \
		echo "[WARN] $(INITRD_DIR) not found. Creating dummy initrd."; \
		touch $(INITRD_TAR); \
	fi
	
# ==============================================================================
# СБОРКА ISO (GRUB-MKRESCUE)
# ==============================================================================
iso: $(KERNEL_BIN) $(INITRD_TAR)
	@echo "[ISO]  Copying kernel to ISO directory..."
	@mkdir -p $(ISO_DIR)/boot
	@cp $(KERNEL_BIN) $(ISO_DIR)/boot/kernel.bin
	@echo "[ISO]  Generating bootable ISO with grub-mkrescue..."
	@grub-mkrescue -o $(ISO_NAME) $(ISO_DIR) 2>/dev/null
	@echo "[ OK ] ISO created: $(ISO_NAME)"

# ==============================================================================
# ОЧИСТКА
# ==============================================================================
clean:
	@echo "[CLEAN] Очистка build/, ISO и временных файлов..."
	@rm -rf $(BUILD_DIR) $(ISO_NAME) $(INITRD_TAR)

# ==============================================================================
# ЗАПУСК (QEMU) С ОТЛАДКОЙ ПРЕРЫВАНИЙ   -d int,cpu_reset -D qemu.log
# ==============================================================================
run: iso
	@echo "[QEMU] Стартуем $(ISO_NAME)..."
	@echo "[DEBUG] Логи прерываний пишутся в qemu.log (ищи Triple Fault по последним записям)"
	@qemu-system-i386 -cdrom $(ISO_NAME) -m 1024M -serial stdio -no-reboot -D qemu.log

.PHONY: all clean run iso
