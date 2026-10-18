#!/usr/bin/env bash
# Ready-to-run QEMU package obj/pint-qemu.tar.gz: the system disk, the ARC firmware, run.sh and
# run.cmd (build/portable: the HDMI window with a USB keyboard and mouse) and qemu-system-arm for
# macOS arm64 (Homebrew's, its libraries bundled with dylibbundler), Linux x86_64 and aarch64
# (AlmaLinux 9, GTK 3 of the host) and Windows x86_64 (Fedora MinGW, GTK 3 bundled), the last
# three built in Docker from the QEMU release. GTK, not SDL: SDL shows the raspi frame buffer
# black. The QEMU binaries stay in obj/portable/bin; FORCE=1 rebuilds them.
#
# Usage:  [IMG=disk.img] [FORCE=1] build/portable-qemu-image.sh
set -euo pipefail
V=${QEMU_VERSION:-11.1.1}
SV=4.9.1

configure_qemu() {
    mkdir -p /work
    tar -C /work -xJf /dl/qemu-$V.tar.xz
    mkdir /work/qemu-$V/b && cd /work/qemu-$V/b
    ../configure --target-list=arm-softmmu --without-default-features --enable-system \
        --enable-tcg --enable-slirp --enable-gtk --enable-pixman --enable-vnc --enable-fdt=internal \
        --enable-stack-protector --disable-docs "$@"
}

linux() {
    dnf -y -q install dnf-plugins-core
    dnf config-manager --set-enabled crb
    dnf -y -q install gcc-toolset-13-gcc gcc-toolset-13-binutils make ninja-build python3.12 \
        glib2-devel pixman-devel gtk3-devel libslirp-devel zlib-devel diffutils findutils bzip2 tar xz git
    set +u; . /opt/rh/gcc-toolset-13/enable; set -u
    configure_qemu --python=/usr/bin/python3.12 --enable-malloc-trim --enable-membarrier
    ninja qemu-system-arm
    local o=/out/linux-$(uname -m)
    rm -rf $o && mkdir -p $o/lib /out/share/qemu/keymaps
    install -m 755 -s qemu-system-arm $o/
    cp -L /usr/lib64/libslirp.so.0 $o/lib/
    readelf -d $o/qemu-system-arm | grep NEEDED
    find ../pc-bios/keymaps -type f ! -name meson.build -exec cp {} /out/share/qemu/keymaps/ \;
    cp ../COPYING ../COPYING.LIB /out/
}

windows() {
    dnf -y -q install gcc mingw64-gcc mingw64-glib2 mingw64-pixman mingw64-gtk3 meson \
        mingw64-adwaita-icon-theme mingw64-hicolor-icon-theme mingw64-pkg-config make ninja-build \
        python3 bzip2 diffutils findutils tar xz git
    tar -C /tmp -xzf /dl/libslirp-v$SV.tar.gz
    (cd /tmp/libslirp-v$SV && meson setup b --cross-file /usr/share/mingw/toolchain-mingw64.meson \
        --prefix=/usr/x86_64-w64-mingw32/sys-root/mingw && ninja -C b install)
    configure_qemu --cross-prefix=x86_64-w64-mingw32-
    ninja qemu-system-arm.exe
    local o=/out/windows-x86_64 m=/usr/x86_64-w64-mingw32/sys-root/mingw f d todo next
    rm -rf $o && mkdir -p $o/lib $o/share/keymaps
    install -m 755 qemu-system-arm.exe $o/
    x86_64-w64-mingw32-strip $o/qemu-system-arm.exe
    cp -r $m/lib/gdk-pixbuf-2.0 $o/lib/
    cp -r $m/share/icons $o/share/
    find ../pc-bios/keymaps -type f ! -name meson.build -exec cp {} $o/share/keymaps/ \;
    todo="$o/qemu-system-arm.exe $(echo $o/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll)"
    while [ -n "$todo" ]; do
        next=
        for f in $todo; do
            for d in $(x86_64-w64-mingw32-objdump -p $f | sed -n 's/^\tDLL Name: //p'); do
                [ -f $o/$d ] || [ ! -f $m/bin/$d ] && continue
                cp $m/bin/$d $o/
                next="$next $o/$d"
            done
        done
        todo=$next
    done
}

case ${1:-} in
--linux) exec >$LOG 2>&1; linux; exit ;;
--windows) exec >$LOG 2>&1; windows; exit ;;
esac

cd "$(dirname "${BASH_SOURCE[0]}")/.."
IMG=${IMG:-obj/nt-arcfw-sd.img}
P=obj/portable
[[ -f obj/arcfw.elf && -f $IMG ]] || { echo "missing obj/arcfw.elf or $IMG - build first"; exit 1; }
command -v dylibbundler >/dev/null && brew --prefix qemu >/dev/null || { echo "needs Homebrew qemu and dylibbundler"; exit 1; }
mkdir -p $P/dl $P/bin $P/log

if [[ ! -s $P/dl/qemu-$V.tar.xz ]]; then
    echo ">> fetching qemu-$V.tar.xz"
    curl -fL# -o $P/dl/qemu-$V.tar.xz.part https://download.qemu.org/qemu-$V.tar.xz
    mv $P/dl/qemu-$V.tar.xz.part $P/dl/qemu-$V.tar.xz
fi
if [[ ! -s $P/dl/libslirp-v$SV.tar.gz ]]; then
    echo ">> fetching libslirp-v$SV.tar.gz"
    curl -fL# -o $P/dl/libslirp-v$SV.tar.gz https://gitlab.freedesktop.org/slirp/libslirp/-/archive/v$SV/libslirp-v$SV.tar.gz
fi

build() {  # NAME DOCKER_PLATFORM IMAGE MODE
    [[ -z ${FORCE:-} && -e $P/bin/$1 ]] && return
    echo ">> building QEMU $V for $1 (log: $P/log/$1.log)"
    docker run --rm ${2:+--platform $2} -v "$PWD/build":/b:ro -v "$PWD/$P/dl":/dl:ro \
        -v "$PWD/$P/bin":/out -v "$PWD/$P/log":/log -e QEMU_VERSION=$V -e LOG=/log/$1.log \
        $3 bash /b/portable-qemu-image.sh $4 || { tail -25 $P/log/$1.log; exit 1; }
}
build linux-x86_64 linux/amd64 almalinux:9 --linux
build linux-aarch64 linux/arm64 almalinux:9 --linux
build windows-x86_64 "" registry.fedoraproject.org/fedora:43 --windows

o=$P/bin/macos-arm64
if [[ -n ${FORCE:-} || ! -e $o ]]; then
    echo ">> bundling Homebrew QEMU for macos-arm64"
    q=$(brew --prefix qemu)/bin/qemu-system-arm
    "$q" --version | grep -q "version $V$" || echo "warning: Homebrew has $("$q" --version | head -1)"
    rm -rf $o && mkdir -p $o
    install -m 755 "$q" $o/
    (cd $o && dylibbundler -od -b -x qemu-system-arm -d lib/ -p @executable_path/lib/ </dev/null) >$P/log/macos-arm64.log 2>&1
    for f in $o/qemu-system-arm $o/lib/*; do
        while otool -l $f | grep -q 'path @executable_path/lib/ '; do
            install_name_tool -delete_rpath @executable_path/lib/ $f 2>>$P/log/macos-arm64.log
        done
    done
    codesign -f -s - $o/lib/*.dylib $o/qemu-system-arm 2>>$P/log/macos-arm64.log
    ! otool -L $o/qemu-system-arm $o/lib/* | grep -E '/opt/homebrew|@rpath' || { echo "unbundled libraries left"; exit 1; }
fi

echo ">> packing obj/pint-qemu.tar.gz"
! lsof -a -c qemu "$IMG" >/dev/null 2>&1 || { echo "a QEMU has $IMG open"; exit 1; }
s=$P/stage r=$P/stage/pint-qemu
rm -rf $s && mkdir -p $r
cp "$IMG" $r/nt-arcfw-sd.img
cp obj/arcfw.elf build/portable/run.sh $r/
sed "s/@V@/$V/g" build/portable/README.txt >$r/README.txt
sed 's/$/\r/' build/portable/run.cmd >$r/run.cmd
cp -R $P/bin $r/bin
COPYFILE_DISABLE=1 tar --no-mac-metadata --no-xattrs --uid 0 --gid 0 --uname root --gname root \
    -C $s -cf - pint-qemu | gzip -9 >obj/pint-qemu.tar.gz
rm -rf $s
echo ">> built obj/pint-qemu.tar.gz ($(du -h obj/pint-qemu.tar.gz | cut -f1))"
