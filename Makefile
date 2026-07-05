# Компиляторы
CC = i686-linux-gnu-gcc
AS = nasm

# Директории и имена
BUILD_DIR = build
ISO_NAME = $(BUILD_DIR)/metal_os.iso

# Флаги компиляции
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude -fno-pie -fno-pic -fno-stack-protector
CFLAGS += -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -mincoming-stack-boundary=2 -g
ASFLAGS = -f elf32 -g
LDFLAGS = -T linker.ld -nostdlib -no-pie -lgcc

# Исходники
C_SOURCES = $(wildcard *.c)
ASM_SOURCES = $(wildcard *.asm)
C_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJS = $(patsubst %.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

# boot.o ПЕРВЫМ!
OBJ = $(BUILD_DIR)/boot.o $(filter-out $(BUILD_DIR)/boot.o,$(C_OBJS) $(ASM_OBJS))
TARGET = $(BUILD_DIR)/metal_os.bin

# ==============================================================================
# СБОРКА
# ==============================================================================
all: iso

$(TARGET): $(OBJ)
	@echo "[LINK] $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "[CC]   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.asm | $(BUILD_DIR)
	@echo "[AS]   $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Сборка ISO (вызывает скрипт)
iso: $(TARGET)
	@echo "[ISO]  Упаковка образа..."
	@./build_iso.sh

clean:
	@echo "[CLEAN] Очистка..."
	@rm -rf $(BUILD_DIR) *.iso

# ==============================================================================
# ЗАПУСК (QEMU) - НИКАКИХ ЗАВИСИМОСТЕЙ ОТ ISO!  -d int,cpu_reset
# ==============================================================================
run:
	@echo "[QEMU] Стартуем $(ISO_NAME)..."
	@qemu-system-i386 -cdrom $(ISO_NAME) -m 512M -serial stdio -no-reboot 

.PHONY: all clean run iso