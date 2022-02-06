Name: drachtio
Version: 0.8.14
Release: 1%{?dist}
Summary: The drachtio server daemon
URL: https://drachtio.org
Group: System Environment/Daemons
License: MIT
Source: %{name}-%{version}.tar.gz

BuildRequires: gcc
BuildRequires: cmake
BuildRequires: rpm-build
BuildRequires: autoconf
BuildRequires: automake
BuildRequires: libtool
BuildRequires: openssl-devel
BuildRequires: zlib-devel

%description
The drachtio server is a SIP server that is built on the Sofia SIP stack. It provides a high-performance SIP engine
that can be controlled by client applications written in pure JavaScript running on Node.js.

%prep
%setup -q -n %{name}-server

%define binname drachtio

%build
./autogen.sh
[ -d build ] && rm -rf build
mkdir build
cd build
../configure CPPFLAGS='-DNDEBUG'
%__make

%pre
getent group %{name} >/dev/null || /usr/sbin/groupadd -r %{name}
getent passwd %{name} >/dev/null || /usr/sbin/useradd -r -g %{name} \
	-s /sbin/nologin -c "%{name} daemon" -d %{_sharedstatedir}/%{name} %{name}

%install
install -D -p -m755 build/%{binname} %{buildroot}%{_bindir}/%{binname}
install -D -p -m644 el/%{binname}.service \
	%{buildroot}%{_unitdir}/%{binname}.service
install -D -p -m644 %{binname}.conf.xml \
	%{buildroot}%{_sysconfdir}/%{binname}/%{binname}.conf.xml
install -d -p -m750 %{buildroot}/var/log/%{binname}

%post
if [ $1 -eq 1 ]; then
	systemctl daemon-reload
fi

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%{_bindir}/%{binname}
%{_unitdir}/%{binname}.service
%attr(0644,drachtio,drachtio) %config(noreplace) %{_sysconfdir}/%{binname}/%{binname}.conf.xml
%attr(0750,drachtio,drachtio) %dir /var/log/%{binname}
%doc README.md
%license LICENSE

%changelog
* Fri Feb 04 2022 Anton Voylenko <anton.voylenko@novait.com.ua>
  - initial version