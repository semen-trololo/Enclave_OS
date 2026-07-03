# Компиляторы
CC = i686-linux-gnu-gcc
AS = nasm

# Директории
BUILD_DIR = build

# Флаги компиляции (Обезвреживание Kali/Debian)
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude -fno-pie -fno-pic -fno-stack-protector
CFLAGS += -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -mincoming-stack-boundary=2
# Флаги ассемблера
ASFLAGS = -f elf32

# Флаги линковки
LDFLAGS = -T linker.ld -nostdlib -no-pie -lgcc

## Автоматический поиск всех исходников
C_SOURCES = $(wildcard *.c)
ASM_SOURCES = $(wildcard *.asm)

# Генерация путей к объектным файлам
C_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJS = $(patsubst %.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

# КРИТИЧЕСКИ ВАЖНО: boot.o должен быть ПЕРВЫМ!
# GRUB ищет Multiboot header в первых 8 КБ файла. Если boot.o будет в конце,
# header окажется за пределами 8 КБ, и GRUB не загрузит ядро.
OBJ = $(BUILD_DIR)/boot.o $(filter-out $(BUILD_DIR)/boot.o,$(C_OBJS) $(ASM_OBJS))

# Финальный бинарник
TARGET = $(BUILD_DIR)/metal_os.bin

# Цель по умолчанию
all: $(TARGET)

# Компоновка (Linking)
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Компиляция C файлов
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Компиляция ASM файлов
$(BUILD_DIR)/%.o: %.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

# Создание папки build, если её нет
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Очистка
clean:
	rm -rf $(BUILD_DIR)

# Создание ISO образа (сначала собирает TARGET, потом запускает скрипт)
iso: $(TARGET)
	@./build_iso.sh

# Запуск через ISO (GRUB)
run: iso
	qemu-system-i386 -cdrom build/metal_os.iso -m 512M -d int,cpu_reset

# Запуск через -kernel (быстрая отладка, может не работать с Higher Half)
run_kernel: $(TARGET)
	qemu-system-i386 -kernel $(TARGET) -m 512M -d int,cpu_reset -no-reboot
	
.PHONY: all clean run iso run_kernel