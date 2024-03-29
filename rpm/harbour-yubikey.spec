Name:           harbour-yubikey
Summary:        YubiKey NFC OTP app
Version:        1.0.8
Release:        1
License:        BSD
URL:            https://github.com/monich/harbour-yubikey
Source0:        %{name}-%{version}.tar.gz

Requires:       sailfishsilica-qt5
Requires:       qt5-qtsvg-plugin-imageformat-svg
Requires:       qt5-qtfeedback

BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(libcrypto)
BuildRequires:  pkgconfig(sailfishapp)
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Multimedia)
BuildRequires:  pkgconfig(Qt5Concurrent)
BuildRequires:  pkgconfig(mlite5)
BuildRequires:  qt5-qttools-linguist

%{!?qtc_qmake5:%define qtc_qmake5 %qmake5}
%{!?qtc_make:%define qtc_make make}
%{?qtc_builddir:%define _builddir %qtc_builddir}

%description
Allows to use Yubikey NFC for storing OTP secrets

%if "%{?vendor}" == "chum"
Categories:
 - Utility
Icon: https://raw.githubusercontent.com/monich/harbour-yubikey/master/icons/harbour-yubikey.svg
Screenshots:
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-001.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-002.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-003.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-004.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-005.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-006.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-007.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-008.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-009.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-010.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-011.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-012.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-013.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-014.png
- https://home.monich.net/chum/harbour-yubikey/screenshots/screenshot-015.png
Url:
  Homepage: https://openrepos.net/content/slava/yubikey-otp
%endif

%prep
%setup -q -n %{name}-%{version}

%build
%qtc_qmake5 %{name}.pro
%qtc_make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

desktop-file-install --delete-original \
  --dir %{buildroot}%{_datadir}/applications \
   %{buildroot}%{_datadir}/applications/*.desktop

%postun
if [ "$1" == 0 ] ; then
  for d in $(getent passwd | cut -d: -f6) ; do
    if [ "$d" != "" ] && [ "$d" != "/" ] && [ -d "$d/.local/share/harbour-yubikey" ] ; then
      rm -fr "$d/.local/share/harbour-yubikey" ||:
    fi
  done
fi

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_sysconfdir}/dbus-1/system.d/harbour.yubikey.conf
%{_sysconfdir}/nfcd/ndef-handlers/_harbour-yubikey.conf
