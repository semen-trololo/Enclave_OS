#!/bin/bash
set -e

echo "[*] Creating ISO directory structure..."
rm -rf isodir
mkdir -p isodir/boot/grub

echo "[*] Copying kernel..."
cp build/metal_os.bin isodir/boot/

echo "[*] Creating GRUB config..."
cat > isodir/boot/grub/grub.cfg << EOF
set timeout=0
set default=0

menuentry "Bare Metal OS (Higher Half)" {
    multiboot /boot/metal_os.bin
    boot
}
EOF

echo "[*] Building ISO image..."
grub-mkrescue -o build/metal_os.iso isodir --compress=xz

echo "[+] ISO created successfully: build/metal_os.iso"
echo "[+] Run with: qemu-system-i386 -cdrom build/metal_os.iso -m 512M"
