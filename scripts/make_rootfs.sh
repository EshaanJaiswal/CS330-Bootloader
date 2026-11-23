#!/bin/sh
# Build a real Alpine userspace as a gzip'd cpio initramfs.
#
# Alpine's stock initramfs-virt is only a bootstrap image: it has drivers and
# an /init script that looks for a separate ISO/squashfs root.  This script
# instead unpacks Alpine's minirootfs into a self-contained root filesystem and
# gives us a serial login prompt, so apk / a normal shell / userspace all work.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=3.24.4
SERIES=${VERSION%.*}                 # 3.24
TARBALL=alpine-minirootfs-${VERSION}-x86_64.tar.gz
URL=https://dl-cdn.alpinelinux.org/alpine/v${SERIES}/releases/x86_64/${TARBALL}
CACHE="$ROOT/build/$TARBALL"
ROOTFS="$ROOT/build/rootfs"
OUTPUT="$ROOT/esp/alpine-rootfs.cpio.gz"

mkdir -p "$ROOT/build" "$ROOT/esp"

if [ ! -s "$CACHE" ]; then
    echo "Downloading $URL"
    curl -fL --retry 3 -o "$CACHE" "$URL"
fi

rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"
tar xzf "$CACHE" -C "$ROOTFS"

# The boot kernel (vmlinuz-virt) keeps its NIC driver as a module.  Reuse the
# modules from Alpine's matching initramfs-virt so virtio-net works in QEMU.
if [ -f "$ROOT/esp/initramfs-virt" ]; then
    mkdir -p "$ROOTFS/lib"
    gzip -dc "$ROOT/esp/initramfs-virt" |
        (cd "$ROOTFS" && cpio -idm --quiet 'lib/modules/*' 2>/dev/null)
fi

# Kernel can start either /init or rdinit=/sbin/init; support both.
ln -sfn sbin/init "$ROOTFS/init"

# BusyBox init (what /sbin/init points to in the minirootfs) reads inittab.
# Mount the pseudo filesystems first, then spawn a login getty on the serial
# line.  ttyS0 is last in the bootloader cmdline, so it becomes /dev/console
# and login I/O works normally on QEMU's serial monitor.
cat > "$ROOTFS/etc/inittab" <<'EOF'
# /etc/inittab -- CS330 graphical and serial consoles
::sysinit:/bin/mount -t proc proc /proc
::sysinit:/bin/mount -t sysfs sysfs /sys
::sysinit:/bin/mount -t devtmpfs devtmpfs /dev
::sysinit:/bin/mkdir -p /run /tmp
::sysinit:/sbin/cs330-net.sh &
::sysinit:/sbin/modprobe vgacon 2>/dev/null
::sysinit:/sbin/modprobe simpledrm 2>/dev/null

tty1::respawn:/sbin/getty 38400 tty1 linux
ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
ttyS1::respawn:/sbin/getty -L 115200 ttyS1 vt100

::ctrlaltdel:/sbin/reboot
::shutdown:/bin/umount -a -r
EOF

# Bring up ethernet with DHCP, falling back to QEMU's documented static
# addressing when DHCP is unavailable.
cat > "$ROOTFS/sbin/cs330-net.sh" <<'EOF'
#!/bin/sh
modprobe virtio_net 2>/dev/null
for i in 1 2 3 4 5 6 7 8 9 10; do
    ip link show eth0 >/dev/null 2>&1 && break
    sleep 1
done

ip link set eth0 up 2>/dev/null || exit 0

if udhcpc -b -i eth0 -q >/dev/null 2>&1; then
    exit 0
fi

# Fallback for QEMU user-networking and networks without DHCP.
ip addr add 10.0.2.15/24 dev eth0 2>/dev/null
ip route add default via 10.0.2.2 2>/dev/null
EOF
chmod +x "$ROOTFS/sbin/cs330-net.sh"

# QEMU user-mode networking gives the guest 10.0.2.3 as its DNS server.
printf 'nameserver 10.0.2.3\n' > "$ROOTFS/etc/resolv.conf"

# Known login: root / cs330.  Edit shadow directly; no root privileges needed
# on the host.  BusyBox login understands the MD5-crypt hash generated here.
HASH=$(openssl passwd -1 -salt cs330 cs330)
sed -i "s|^root:[^:]*:|root:${HASH}:|" "$ROOTFS/etc/shadow"

cat > "$ROOTFS/etc/issue" <<'EOF'

  CS330 UEFI Bootloader -> Alpine Linux
  login as root, password: cs330

EOF

echo "Alpine userspace ready: $ROOTFS"
echo "Building $OUTPUT ..."
(cd "$ROOTFS" && find . -print0 | cpio --null -ov --format=newc 2>/dev/null | gzip -9 > "$OUTPUT")
ls -lh "$OUTPUT"
