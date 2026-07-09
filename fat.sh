#!/bin/bash
set -e

DISK_IMG="disk.img"
SIZE="100M"
OFFSET=1048576  # 2048 секторов * 512 байт = 1 MiB
PART_SIZE=202752  # Размер раздела в секторах (из вывода sfdisk)

echo "[1/5] Creating empty disk image..."
rm -f $DISK_IMG
qemu-img create -f raw $DISK_IMG $SIZE

echo "[2/5] Creating MBR and FAT32 partition..."
echo "2048,,c,*" | sfdisk $DISK_IMG

echo "[3/5] Formatting partition as FAT32..."
# Правильный синтаксис mformat:
# -i = image file
# @@offset = offset в байтах
# -T = total sectors в разделе
# -h = heads (для совместимости)
# -s = sectors per track
# -F = FAT32
mformat -i $DISK_IMG@@$OFFSET -T $PART_SIZE -h 64 -s 32 -F -v "BAREMETAL" ::

echo "[OK] disk.img created successfully!"
