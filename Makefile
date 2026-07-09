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

# Флаги компиляции (Строго по Базе Знаний)
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
CFLAGS += -fno-pie -fno-pic -fno-stack-protector
CFLAGS += -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -mincoming-stack-boundary=2 -g
CFLAGS += -MMD -MP  # Автоматическая генерация зависимостей от .h файлов

ASFLAGS = -f elf32 -g
LDFLAGS = -T linker.ld -nostdlib -no-pie -lgcc

# ==============================================================================
# ИСХОДНИКИ И ОБЪЕКТЫ
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
# ГЛАВНЫЕ ТАРГЕТЫ
# ==============================================================================
all: iso

# Линковка ядра
$(KERNEL_BIN): $(OBJ) linker.ld
	@echo "[LINK] $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter-out linker.ld,$^)

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
# ПРОВЕРКИ ПЕРЕД СБОРКОЙ ISO
# ==============================================================================
check_prerequisites:
	@echo "[CHECK] Verifying prerequisites..."
	@if [ ! -f "linker.ld" ]; then \
		echo "[FATAL] linker.ld not found!"; \
		exit 1; \
	fi
	@if [ ! -f "$(ISO_DIR)/boot/grub/grub.cfg" ]; then \
		echo "[FATAL] $(ISO_DIR)/boot/grub/grub.cfg not found!"; \
		echo "[HINT] Create grub.cfg with:"; \
		echo "  set timeout=0"; \
		echo "  set default=0"; \
		echo "  menuentry \"Bare Metal OS\" {"; \
		echo "    multiboot /boot/kernel.bin"; \
		echo "    module /boot/initrd.tar"; \
		echo "    boot"; \
		echo "  }"; \
		exit 1; \
	fi
	@echo "[ OK ] All prerequisites met."

# ==============================================================================
# СБОРКА ISO (GRUB-MKRESCUE)
# ==============================================================================
iso: check_prerequisites $(KERNEL_BIN) $(INITRD_TAR)
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
	@rm -f $(ISO_DIR)/boot/kernel.bin

# ==============================================================================
# ЗАПУСК (QEMU) С ОТЛАДКОЙ
# ==============================================================================
run: iso
	@echo "[QEMU] Стартуем $(ISO_NAME)..."
	@echo "[DEBUG] Логи прерываний пишутся в qemu.log"
	@qemu-system-i386 -cdrom $(ISO_NAME) -m 1024M -serial stdio -no-reboot -D qemu.log

# Запуск с ATA диском (для тестирования ATA драйвера)
run_ata: iso
	@echo "[QEMU] Стартуем с ATA диском..."
	@qemu-system-i386 -cdrom $(ISO_NAME) -m 1024M -serial stdio -no-reboot \
		-boot d \
		-drive file=disk.img,format=raw,if=ide,index=0,media=disk

# Создать пустой ATA диск для тестирования
create_disk:
	@echo "[DISK] Создаем пустой ATA диск (100 MB)..."
	@qemu-img create -f raw disk.img 100M
	@echo "[ OK ] disk.img создан"

.PHONY: all clean run run_ata create_disk iso check_prerequisites

# Включаем автоматически сгенерированные зависимости
-include $(DEPS)