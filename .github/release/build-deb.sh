#!/bin/bash
# Build the cde-desktop .deb (and a plain .tar.gz of its files) for the
# architecture of the container this runs in (Debian; emulated or not).
# usage: build-deb.sh VERSION   (source at /src, results in /src/out)
set -Eeuo pipefail
version=$1
export DEBIAN_FRONTEND=noninteractive IFFEFLAGS=-d1 LANG=C.UTF-8
apt-get -o Acquire::Retries=3 update
apt-get -o Acquire::Retries=3 install -y --no-install-recommends \
    build-essential devscripts equivs fakeroot ca-certificates
arch=$(dpkg --print-architecture)
work=/build/cde-$arch
rm -rf "$work"; mkdir -p "$work"
cp -a /src/cde/. "$work/"
cd "$work"
# this release's version on top of the changelog
{
  printf 'cde-desktop (%s-1) unstable; urgency=medium\n\n' "$version"
  printf '  * Clanker Desktop Environment %s.\n\n' "$version"
  printf ' -- Clanker CDE release <noreply@github.com>  %s\n\n' "$(date -R)"
  cat debian/changelog
} > debian/changelog.new
mv debian/changelog.new debian/changelog
mk-build-deps --install --remove \
    --tool 'apt-get -y -o Acquire::Retries=3 --no-install-recommends' debian/control
dpkg-buildpackage -b -us -uc -j"$(nproc)"
mkdir -p /src/out
cp ../cde-desktop*_"$arch".deb /src/out/
# the same files as a plain tarball, for systems without dpkg
deb=$(ls ../cde-desktop_*_"$arch".deb | head -1)
rm -rf /tmp/root; mkdir -p /tmp/root
dpkg-deb -x "$deb" /tmp/root
tar -C /tmp/root -czf "/src/out/clanker-cde-$version-linux-$arch.tar.gz" .
ls -la /src/out
