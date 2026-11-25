#!/bin/bash
# Build the clanker-cde RPM in a Fedora container (its own architecture).
# usage: build-rpm.sh VERSION   (source at /src, results in /src/out)
set -Eeuo pipefail
version=$1
export LANG=C.UTF-8
dnf -y install rpm-build rpmdevtools tar gzip git \
    autoconf automake bdftopcf binutils bison cpp diffutils file findutils \
    flex freetype-devel gcc gcc-c++ glibc-langpack-en gzip ksh libICE-devel \
    libSM-devel libX11-devel libXau-devel libXaw-devel libXdmcp-devel \
    libXext-devel libXft-devel libXinerama-devel libXmu-devel libXpm-devel \
    libXrender-devel libXScrnSaver-devel libXi-devel libXtst-devel \
    libXrandr-devel dbus-devel libXt-devel libjpeg-turbo-devel libpng-devel \
    libtirpc-devel libtool libutempter-devel libxcrypt-devel lmdb-devel m4 \
    make mkfontscale motif-devel ncompress opensp openssl-devel pam-devel \
    patch perl-interpreter pkgconf-pkg-config python3 rpcgen sessreg tar \
    tcl-devel xrdb xorg-x11-proto-devel
rpmdev-setuptree
src=clanker-cde-$version
rm -rf "/tmp/$src"; mkdir -p "/tmp/$src"
cp -a /src/cde "/tmp/$src/"
tar -C /tmp -czf ~/rpmbuild/SOURCES/$src.tar.gz "$src"
rpmbuild -bb --define "version $version" /src/.github/release/clanker-cde.spec
mkdir -p /src/out
find ~/rpmbuild/RPMS -name '*.rpm' -exec cp {} /src/out/ \;
ls -la /src/out
