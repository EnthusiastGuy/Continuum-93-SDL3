#!/bin/sh
# Boot-image test in QEMU (KVM), run in the container from boot/test/Dockerfile (docs/BOOT.md, "Testing"):
#   run.sh <img> <bios|uefi|uefi32|rpi3> <seconds> [steps...]
# Screenshots after <seconds> (/out/<fw>/shot1.png), runs the steps (a QEMU sendkey name, m:<monitor
# command> such as "m:mouse_move 50 50", or t:<text> typed + Enter), screenshots again (shot2.png), then
# copies the stick's logs to /out/<fw>/continuum93 and its Data/init.cfg to /out/<fw>. Audio lands in audio.wav, serial in serial.txt.
# Env: QEMU="qemu-system-i386 -cpu host" for x86; KERNEL=/img/BOOTX64.EFI APPEND="console=ttyS0 ..." to
# boot the kernel directly with extra arguments (the serial console shows init's and the emulator's output);
# DISK=512M enlarges the image first (a bigger stick) and keeps it as /out/<fw>/disk.img.
IMG=$1 FW=$2 T=$3; shift 3
O=/out/$FW; rm -rf $O; mkdir -p $O
cp $IMG /tmp/t.img
[ -n "$DISK" ] && truncate -s $DISK /tmp/t.img   # a bigger stick: init grows the partition on first boot
FWARGS= MACHINE=q35
if [ $FW = uefi32 ]; then
  cp /usr/share/OVMF/OVMF32_VARS_4M.fd /tmp/vars.fd; MACHINE=q35,smm=on
  FWARGS="-global driver=cfi.pflash01,property=secure,value=on -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF32_CODE_4M.secboot.fd -drive if=pflash,format=raw,file=/tmp/vars.fd"
fi
if [ $FW = uefi ]; then
  cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/vars.fd
  FWARGS="-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd -drive if=pflash,format=raw,file=/tmp/vars.fd"
fi
if [ $FW = rpi3 ]; then
  # A Pi 3 (TCG, no KVM): QEMU stands in for the Pi's firmware, so it is given kernel8.img and the device tree
  # off the card, and no config.txt overlay: there is no vc4 display, the serial console shows the boot instead.
  export MTOOLS_SKIP_CHECK=1
  mcopy -n -i /tmp/t.img@@1048576 ::/kernel8.img ::/bcm2710-rpi-3-b.dtb /tmp/
  qemu-system-aarch64 -M raspi3b -kernel /tmp/kernel8.img -dtb /tmp/bcm2710-rpi-3-b.dtb \
    -drive if=sd,format=raw,file=/tmp/t.img -usb -device usb-kbd -device usb-mouse \
    -append "console=ttyAMA0 loglevel=6 ${APPEND}" \
    -display none -serial file:$O/serial.txt -monitor unix:/tmp/mon,server,nowait &
else
QEMU=${QEMU:-qemu-system-x86_64}
$QEMU -enable-kvm -m 1024 -smp 2 -machine $MACHINE $FWARGS -vga std \
  -device qemu-xhci -drive if=none,id=stick,format=raw,file=/tmp/t.img -device usb-storage,drive=stick \
  -device usb-kbd -device usb-mouse \
  -audiodev wav,id=a,path=$O/audio.wav -device intel-hda -device hda-duplex,audiodev=a \
  -display none -serial file:$O/serial.txt -monitor unix:/tmp/mon,server,nowait ${KERNEL:+-kernel $KERNEL} ${APPEND:+-append "$APPEND"} &
fi
mon() { echo "$1" | socat - unix-connect:/tmp/mon >> $O/mon.txt; }
# type <text>: one sendkey per char, then Enter
type() { echo "$1" | fold -w1 | while IFS= read -r c; do case "$c" in
  " ") n=spc;; /) n=slash;; "|") n=shift-backslash;; -) n=minus;; .) n=dot;; ,) n=comma;; "*") n=shift-8;; ">") n=shift-dot;; [A-Z]) n=shift-$(echo $c | tr A-Z a-z);; *) n=$c;; esac
  mon "sendkey $n"; done; mon "sendkey ret"; sleep 1; }
sleep $T; mon "screendump $O/shot1.png -f png"
if [ $# -gt 0 ]; then for k in "$@"; do case $k in m:*) mon "${k#m:}";; t:*) type "${k#t:}";; *) mon "sendkey $k";; esac; sleep 0.3; done; sleep 3; mon "screendump $O/shot2.png -f png"; fi
kill -0 $! 2>/dev/null && echo "VM still running" || echo "VM exited by itself"; mon quit; wait
export MTOOLS_SKIP_CHECK=1
mcopy -s -n -i /tmp/t.img@@1048576 ::/continuum93 $O/ 2>/dev/null
mcopy -n -i /tmp/t.img@@1048576 ::/Data/init.cfg $O/ 2>/dev/null
[ -n "$DISK" ] && cp /tmp/t.img $O/disk.img   # boot it again: run.sh /out/<fw>/disk.img ...
ls -la $O
