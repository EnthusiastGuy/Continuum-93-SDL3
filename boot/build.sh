#!/bin/sh
# Builds a bootable image inside the boot container (boot/Dockerfile); `build boot` runs it.
#   sh boot/build.sh x64|x86|rpi [userland|image]   /src = the repo, /cache = a Docker volume (case-sensitive work space)
# userland (steps 1-3: SDL, the emulator, the initramfs files staged in /cache) runs on the target's arch; image
# (steps 4-5: kernel, stick) on any: the Pi's kernel is cross-built on x64 by clang, emulated it would take hours.
# No stage = both. Output: build/boot-<arch>/continuum93-boot-<arch>.img and dist/continuum93-boot-<arch>.zip.
# The stick is one FAT32 partition "C93" with Data/ beside the boot files, editable from any OS. PCs: the kernel
# (initramfs inside) is \EFI\BOOT\BOOT*.EFI for UEFI, syslinux starts the same file on BIOS PCs. Pi: the Pi's
# firmware, kernel8.img and the device trees.
set -e
ARCH=$1 STAGE=${2:-all}
KVER=6.18.60
case $ARCH in
  x64) KARCH=x86_64 EFI=BOOTX64.EFI XFLAGS= ;;
  # SSE2 (Pentium 4 / Athlon 64 on): x87's extended precision would change float results.
  x86) KARCH=i386 EFI=BOOTIA32.EFI XFLAGS="-msse2 -mfpmath=sse" ;;
  # Raspberry Pi's kernel tree at the same Linux version (its SoC drivers and device trees), and its boot firmware.
  rpi) KARCH=arm64 XFLAGS= RPITAG=stable_20261103 FWTAG=1.20260915 ;;
  *) echo "usage: boot/build.sh x64|x86|rpi [userland|image]"; exit 1 ;;
esac
OUT=/src/build/boot-$ARCH
W=/cache/$ARCH
R=$W/root   # the initramfs files, staged so the image stage can run in another container
L=$W/initramfs.list
mkdir -p $OUT $W /src/dist

if [ $STAGE != image ]; then
# 1. SDL at CMakeLists.txt's tag plus boot/sdl-bare.patch: dumb buffers instead of Mesa, /dev/input scan instead of udev.
TAG=$(sed -n 's/.*GIT_TAG \(release-3[0-9.]*\).*/\1/p' /src/CMakeLists.txt)
SDL=/cache/sdl-$TAG-$(md5sum < /src/boot/sdl-bare.patch | cut -c1-8)
if [ ! -f $SDL/.patched ]; then
    rm -rf $SDL
    git -c advice.detachedHead=false clone -q --depth 1 -b $TAG https://github.com/libsdl-org/SDL.git $SDL
    git -C $SDL apply /src/boot/sdl-bare.patch
    touch $SDL/.patched
fi

# 2. The emulator: KMS/DRM, evdev and ALSA only.
cmake -S /src -B $W/app -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_SDL3=$SDL \
    -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_LIBUDEV=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF \
    -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DCMAKE_C_FLAGS="$XFLAGS" -DCMAKE_CXX_FLAGS="$XFLAGS" >/dev/null
cmake --build $W/app

# 3. The initramfs as a gen_init_cpio list (no root needed for /dev/console): init, busybox, the emulator,
#    the libraries it loads (dlopen'd libdrm and libasound too), ALSA's configuration and amixer.
APP=$W/app/continuum
LIBDIR=$(dirname "$(ldd $APP | grep -o '/[^ ]*/libc\.so\.6')")
LIBS="$LIBDIR/libdrm.so.2 $LIBDIR/libasound.so.2"
LIBS=$(printf '%s\n' $LIBS $(ldd $APP /usr/bin/amixer $LIBS | grep -o '/[^ :]*\.so[^ :]*') | sort -u)
rm -rf $R
: > $L.dirs
: > $L.files
add() {   # add <path in image> <source> <mode>
    mkdir -p $R$(dirname $1) && cp -L $2 $R$1
    d=$(dirname $1)
    while [ "$d" != / ]; do echo "dir $d 755 0 0" >> $L.dirs; d=$(dirname $d); done
    echo "file $1 $R$1 $3 0 0" >> $L.files
}
add /init /src/boot/init 755
add /bin/busybox /bin/busybox 755
add /bin/amixer /usr/bin/amixer 755
add /c93/continuum $APP 755
for f in $LIBS; do add $f $f 755; done
for f in $(find /usr/share/alsa -type f); do add $f $f 644; done
printf 'root:x:0:0:root:/tmp:/bin/sh\n' > $W/passwd
printf 'root:x:0:\naudio:x:29:\n' > $W/group   # ALSA's dmix wants the audio group
add /etc/passwd $W/passwd 644
add /etc/group $W/group 644
{
    printf 'dir /dev 755 0 0\nnod /dev/console 600 0 0 c 5 1\n'
    printf 'dir /proc 755 0 0\ndir /sys 755 0 0\ndir /mnt 755 0 0\ndir /tmp 1777 0 0\n'
    sort -u $L.dirs
    cat $L.files
    echo 'slink /c93/Data /mnt/c93/Data 777 0 0'   # SDL_GetBasePath() is /c93: Data/ is the stick's
} > $L
fi
[ $STAGE = userland ] && exit 0

# 4. The kernel, the initramfs inside, no modules. PCs: tinyconfig + kernel.config + kernel-pc.config (+ x86).
#    Pi: the Pi's defconfig, modules dropped, + kernel.config + kernel-rpi.config; clang cross-builds it.
if [ $ARCH = rpi ]; then
    K=/cache/linux-rpi-$RPITAG URL=https://github.com/raspberrypi/linux/archive/refs/tags/$RPITAG.tar.gz X=-xz
else
    K=/cache/linux-$KVER URL=https://cdn.kernel.org/pub/linux/kernel/v${KVER%%.*}.x/linux-$KVER.tar.xz X=-xJ
fi
if [ ! -f $K/Makefile ]; then
    rm -rf $K.tmp && mkdir -p $K.tmp
    curl -fsSL $URL | tar $X -C $K.tmp --strip-components=1
    mv $K.tmp $K
fi
KO=$W/kernel
LLVM=
[ $ARCH = rpi ] && LLVM=LLVM=-16
mk() { env -u CC -u CXX -u LDFLAGS make -C $K O=$KO ARCH=$KARCH $LLVM -j"$(nproc)" "$@"; }
F=$W/kernel.fragment
cat /src/boot/kernel.config > $F
if [ $ARCH = rpi ]; then cat /src/boot/kernel-rpi.config >> $F; else cat /src/boot/kernel-pc.config >> $F; fi
[ $ARCH = x86 ] && cat /src/boot/kernel-x86.config >> $F
echo "CONFIG_INITRAMFS_SOURCE=\"$L\"" >> $F
if [ $ARCH = rpi ]; then mk -s bcm2711_defconfig >/dev/null; else mk -s tinyconfig >/dev/null; fi
$K/scripts/kconfig/merge_config.sh -m -O $KO $KO/.config $F >/dev/null
# mod2noconfig also fills in new symbols like olddefconfig, but turns every module into "no".
if [ $ARCH = rpi ]; then mk -s mod2noconfig >/dev/null; else mk -s olddefconfig >/dev/null; fi
missing=$(grep '^CONFIG_' $F | while IFS= read -r l; do grep -qxF "$l" $KO/.config || echo "$l"; done)
if [ -n "$missing" ]; then echo "kernel: Kconfig dropped (missing dependency?):"; echo "$missing"; exit 1; fi
if [ $ARCH = rpi ]; then
    mk -s Image.gz dtbs
    cp $KO/arch/arm64/boot/Image.gz $OUT/kernel8.img
    KFILE=kernel8.img
else
    # A 32-bit zstd cannot allocate the window of the kernel's -22: x86 compresses at -19 (the last level wins).
    Z=
    if [ $ARCH = x86 ]; then printf '#!/bin/sh\nexec zstd "$@" -19\n' > $W/zstd && chmod +x $W/zstd && Z=ZSTD=$W/zstd; fi
    mk -s bzImage $Z
    cp $KO/arch/x86/boot/bzImage $OUT/$EFI
    KFILE=$EFI
fi

# 5. The stick: MBR, one bootable FAT32 partition "C93" from 1 MiB.
IMG=$OUT/continuum93-boot-$ARCH.img
MB=64
rm -f $IMG
truncate -s ${MB}M $IMG
echo 'start=2048, type=c, bootable' | sfdisk -q $IMG
P="$IMG@@1048576"
mformat -i "$P" -F -v C93 -T $(( (MB - 1) * 2048 )) ::
mcopy -i "$P" -s /src/Data ::/
# The emulator's default init.cfg (main.cpp kDefaults), CRT filter off: firmware framebuffers can be big and the CPU small.
sed -n '/kDefaults = R"(/,/^)";/p' /src/src/main.cpp | sed '1s/.*R"(//; $d; s/^crtFilter=true/crtFilter=false/' > $W/init.cfg
grep -qx 'crtFilter=false' $W/init.cfg || { echo "boot: no crtFilter line in main.cpp's kDefaults"; exit 1; }
mcopy -i "$P" $W/init.cfg ::/Data/init.cfg
if [ $ARCH = rpi ]; then
    # The Pi's closed boot firmware (Broadcom's licence allows redistribution; LICENCE.broadcom goes along).
    # bootcode.bin + start/fixup: Pi 3 and Zero 2 W; start4/fixup4: Pi 4 and 400; the Pi 5 boots from its EEPROM.
    FW=/cache/rpi-firmware-$FWTAG
    if [ ! -f $FW/.done ]; then
        mkdir -p $FW
        for f in bootcode.bin start.elf fixup.dat start4.elf fixup4.dat LICENCE.broadcom; do
            curl -fsSL -o $FW/$f https://raw.githubusercontent.com/raspberrypi/firmware/$FWTAG/boot/$f
        done
        touch $FW/.done
    fi
    D=$KO/arch/arm64/boot/dts
    mcopy -i "$P" $FW/* $OUT/kernel8.img $D/broadcom/bcm2710-rpi-*.dtb $D/broadcom/bcm2711-rpi-*.dtb $D/broadcom/bcm2712*-rpi-*.dtb ::/
    mmd -i "$P" ::/overlays
    mcopy -i "$P" $D/overlays/overlay_map.dtb $D/overlays/vc4-kms-v3d*.dtbo ::/overlays/
    cat > $W/config.txt <<EOF
# Continuum 93: the 64-bit kernel, the display through KMS (vc4) at the monitor's own mode, no splash or delay.
arm_64bit=1
kernel=kernel8.img
dtoverlay=vc4-kms-v3d
dtparam=audio=on
disable_splash=1
boot_delay=0
disable_overscan=1
EOF
    # delay_use=0: mount a USB stick without usb-storage's 1 s settle.
    printf 'console=tty1 quiet loglevel=3 usb-storage.delay_use=0\n' > $W/cmdline.txt
    mcopy -i "$P" $W/config.txt $W/cmdline.txt ::/
else
    mmd -i "$P" ::/EFI ::/EFI/BOOT ::/syslinux
    mcopy -i "$P" $OUT/$EFI ::/EFI/BOOT/$EFI
    cat > $W/syslinux.cfg <<EOF
# BIOS boot. vga= picks the VESA screen mode: 0x318 is 1024x768 (0x31B 1280x1024, 0x315 800x600).
DEFAULT c93
PROMPT 0
LABEL c93
  LINUX /EFI/BOOT/$EFI
  APPEND vga=0x318
EOF
    mcopy -i "$P" $W/syslinux.cfg ::/syslinux/syslinux.cfg
    syslinux --offset 1048576 --directory /syslinux --install $IMG
    dd if=/usr/lib/syslinux/mbr/mbr.bin of=$IMG bs=440 count=1 conv=notrunc status=none
fi
rm -f /src/dist/continuum93-boot-$ARCH.zip
(cd $OUT && zip -q /src/dist/continuum93-boot-$ARCH.zip continuum93-boot-$ARCH.img)
echo "boot: $IMG ($(du -k $OUT/$KFILE | cut -f1) KB kernel), dist/continuum93-boot-$ARCH.zip"
