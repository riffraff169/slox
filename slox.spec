%define debug_package %{nil}

Name:       slox
Version:    1.4.1
Release:    2%{?dist}
Summary:    The slox bytecode virtual machine and custom runtime

License:    MIT
URL:        https://github.com/riffraff169/slox
Source0:    %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  readline-devel
BuildRequires:  pcre2-devel
BuildRequires:  pkgconfig

%package vim
Summary:        Vim support for the Lox programming language
Requires:       vim-common
BuildArch:      noarch

%description vim
Vim syntax highlighting, filetype detection, and indentation plugins
for slox (.lox) source files

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
mkdir -p %{buildroot}/usr/share/slox/tests
mkdir -p %{buildroot}/usr/share/slox/scripts

install -m 0755 bin/slox %{buildroot}/usr/local/bin/slox

cp lib/*.lox %{buildroot}/usr/local/lib/slox

cp modules/*.so %{buildroot}/usr/local/lib/slox/modules

cp examples/* %{buildroot}/usr/share/slox/examples
cp -r tests/* %{buildroot}/usr/share/slox/tests
cp scripts/* %{buildroot}/usr/share/slox/scripts/
cp docs/* %{buildroot}/usr/share/slox/

mkdir -p %{buildroot}%{_datadir}/vim/vimfiles/ftdetect
mkdir -p %{buildroot}%{_datadir}/vim/vimfiles/ftplugin
mkdir -p %{buildroot}%{_datadir}/vim/vimfiles/syntax

cp extras/lox-ftdetect.vim %{buildroot}%{_datadir}/vim/vimfiles/ftdetect/lox.vim
cp extras/lox-ftplugin.vim %{buildroot}%{_datadir}/vim/vimfiles/ftplugin/lox.vim
cp extras/lox-syntax.vim %{buildroot}%{_datadir}/vim/vimfiles/syntax/lox.vim

%clean
rm -rf %{buildroot}

%files
/usr/local/bin/slox
/usr/local/lib/slox/
/usr/share/slox/

%doc README.md

%files vim
%{_datadir}/vim/vimfiles/ftdetect/lox.vim
%{_datadir}/vim/vimfiles/ftplugin/lox.vim
%{_datadir}/vim/vimfiles/syntax/lox.vim

%changelog
* Fri  Jul 31 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.1-2
- updated spec and Makefile

* Fri  Jul 17 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.0-1
- More features

* Fri  Jul 17 2026 Lance Dillon <riffraff169@yahoo.com> - 1.1.0-1
- Lots of tests added

* Sat  Jul 04 2026 Lance Dillon <riffraff169@yahoo.com> - 1.0.0-1
- Modularized directory layout integration
- Added cascading path lookups for native extensions and stdlib
