%define debug_package %{nil}

Name:       slox
Version:    1.0.1
Release:    1%{?dist}
Summary:    The slox bytecode virtual machine and custom runtime

License:    MIT
URL:        https://github.com/riffraff169/slox
Source0:    %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  readline-devel
BuildRequires:  pcre2-devel
BuildRequires:  pkgconfig

# Disable standard RPM build-root policies that block installations to /usr/local
%define _unpackaged_files_terminate_build 0

%description
An optimized variant of the Lox VM featuring a recurseive descent serializer,
custom native C modules, and a cascading search path for its standard library.

%prep
%setup -q

%build
# Compile both the binary target and the sub-modules via your root Makeifle
make %{?_smp_mflags}

%install
rm -rf %{buildroot}

mkdir -p %{buildroot}/usr/local/bin
mkdir -p %{buildroot}/usr/local/lib/slox/modules
mkdir -p %{buildroot}/usr/share/slox/examples

install -m 0755 bin/slox %{buildroot}/usr/local/bin/slox

cp lib/*.lox %{buildroot}/usr/local/lib/slox

cp modules/*.so %{buildroot}/usr/local/lib/slox/modules

cp examples/* %{buildroot}/usr/share/slox/examples

%clean
rm -rf %{buildroot}

%files
/usr/local/bin/slox
/usr/local/lib/slox/
/usr/share/slox/

%doc README.md

%changelog
* Sat  Jul 04 2026 Lance Dillon <riffraff169@yahoo.com> - 1.0.0-1
- Modularized directory layout integration
- Added cascading path lookups for native extensions and stdlib
