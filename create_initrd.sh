#!/bin/bash

# Цвета для красивого вывода
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${CYAN}=== Bare Metal OS: Initrd Generator ===${NC}"

# 1. Очистка старой структуры (если была)
rm -rf initrd_src
rm -f isodir/boot/initrd.tar

# 2. Создание директорий
echo -e "${YELLOW}[1/4] Creating directory structure...${NC}"
mkdir -p initrd_src/boot
mkdir -p initrd_src/tmp

# 3. Создание тестовых файлов
echo -e "${YELLOW}[2/4] Populating files...${NC}"
echo "Hello from Bare Metal OS VFS!" > initrd_src/hello.txt
echo "TOP SECRET KERNEL DATA" > initrd_src/boot/secret.txt
echo "User writable sandbox" > initrd_src/tmp/test.txt

# 4. Проверка наличия isodir/boot/
if [ ! -d "isodir/boot" ]; then
    echo -e "${RED}[ERR] Directory 'isodir/boot' not found! Run 'make' first or create it manually.${NC}"
    mkdir -p isodir/boot
fi

# 5. Упаковка в TAR (КРИТИЧНО: --format=ustar, иначе парсер initrd.c не увидит magic)
echo -e "${YELLOW}[3/4] Packing TAR archive (UStar format)...${NC}"
(cd initrd_src && tar --format=ustar -cvf ../isodir/boot/initrd.tar .)

# 6. Вывод результата
echo -e "${GREEN}[4/4] Done! Archive created: isodir/boot/initrd.tar${NC}"
echo -e "\n${CYAN}Archive contents:${NC}"
tar -tvf isodir/boot/initrd.tar

echo -e "\n${CYAN}=== Next Steps ===${NC}"
echo -e "1. Ensure your ${YELLOW}isodir/boot/grub/grub.cfg${NC} contains:"
echo -e "   ${GREEN}module /boot/initrd.tar${NC}"
echo -e "2. Build and run ISO:"
echo -e "   ${GREEN}make iso${NC}"
echo -e "   ${GREEN}make run${NC}"
echo -e "\n${CYAN}=== Shell Test Commands ===${NC}"
echo -e "   ${GREEN}ls /${NC}                  (Expect: boot, tmp, hello.txt)"
echo -e "   ${GREEN}cat /hello.txt${NC}        (Expect: Hello from Bare Metal OS VFS!)"
echo -e "   ${RED}cat /boot/secret.txt${NC}  (Expect: Error -13 / EACCES - FS_SYSTEM protection!)"
