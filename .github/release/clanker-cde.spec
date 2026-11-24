# The Clanker Desktop Environment (a CDE fork), installed in /usr/dt as
# CDE always was.  Built by .github/release/build-rpm.sh.
%global debug_package %{nil}
%global __brp_mangle_shebangs %{nil}

Name:           clanker-cde
Version:        %{version}
Release:        1%{?dist}
Summary:        Clanker Desktop Environment, a modernized CDE
License:        LGPL-2.0-or-later
URL:            https://github.com/astartes-gaming/clanker-desktop-environment
Source0:        %{name}-%{version}.tar.gz
Requires:       ksh, rpcbind, xorg-x11-xinit
Provides:       cde

%description
The Common Desktop Environment with a modern session: application menu
with search and the Windows key, Front Panel drawers, system tray, close
buttons, kitty/Ghostty terminal theming, D-Bus, logind and touch
support.  Installs in /usr/dt.

%prep
%autosetup -n %{name}-%{version}

%build
cd cde
./autogen.sh
%configure --prefix=/usr/dt --bindir=/usr/dt/bin --libdir=/usr/dt/lib \
    --libexecdir=/usr/dt/libexec --mandir=/usr/dt/share/man \
    KSH=/usr/bin/ksh
%make_build

%install
# CDE's programs find their libraries in /usr/dt/lib through a runpath,
# which Fedora's check-rpaths would refuse
export QA_RPATHS=$(( 0x0001|0x0002 ))
cd cde
test ! -f programs/dtsearchpath/dtappg/Makefile || \
    sed -i -E '/^[[:space:]]+chown[[:space:]]+root[[:space:]].*dtappgather/s@^.*@	: chown at install@' \
        programs/dtsearchpath/dtappg/Makefile
%make_install
# the login session entries
install -D -m 0644 contrib/desktopentry/cde.desktop \
    %{buildroot}/usr/share/xsessions/cde.desktop
install -D -m 0644 contrib/desktopentry/cde-xwayland.desktop \
    %{buildroot}/usr/share/wayland-sessions/cde-xwayland.desktop
# libtool archives are not shipped
find %{buildroot} -name '*.la' -delete
# the file list: everything installed
cd %{buildroot}
find . \( -type f -o -type l \) | sed 's|^\.||; s|^\(.*\)$|"\1"|' > %{_builddir}/files.list
find ./usr/dt ./etc/dt -type d 2>/dev/null | sed 's|^\.||; s|^\(.*\)$|%dir "\1"|' >> %{_builddir}/files.list || :

%post
/sbin/ldconfig
%postun
/sbin/ldconfig

%files -f %{_builddir}/files.list

%changelog
* Tue Nov 24 2026 Clanker CDE <noreply@github.com> - %{version}-1
- Release built by GitHub Actions
