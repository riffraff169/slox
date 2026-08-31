%debug_package

%bcond_without gtk
%bcond_without sqlite
%bcond_without postgres
%bcond_without yaml
%bcond_without notcurses

Name:       slox
Version: 1.5.18
Release: 1%{?dist}
Summary:    The slox bytecode virtual machine and custom runtime

License:    MIT
URL:        https://github.com/riffraff169/slox
Source0:    %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  readline-devel
BuildRequires:  pcre2-devel
BuildRequires:  pkgconfig
BuildRequires:  ca-certificates

Requires:   readline
Requires:   pcre2
Requires:   libffi
Requires:   ca-certificates

%description
An optimized variant of the Lox VM featuring a recurseive descent serializer,
custom native C modules, and a cascading search path for its standard library.

# GTK4 / GObject Introspection Subpackage
%if %{with gtk}
%package gtk
Summary:        GTK4 and GObject Introspection bindings for slox
BuildRequires:  gobject-introspection-devel
BuildRequires:  gtk4-devel
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description gtk
Provideds the liblogx_gi.so native module for building graphical UI
applications using GTK4 in slox.
%endif

# SQLite Subpackage
%if %{with sqlite}
%package sqlite
Summary:        SQLite database bindings for slox
BuildRequires:  sqlite-devel
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description sqlite
Provides the liblox_sqlite.so native C module and sqlite_ext.lox
helper library for slox.
%endif

# postgres
%if %{with postgres}
%package postgres
Summary:        PostgreSQL database bindings for slox
BuildRequires:  postgresql-devel
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description postgres
Provides the liblox_pg.so native C module and pg_ext.lox
helper library for slox.
%endif

# yaml
%if %{with yaml}
%package yaml
Summary:        YAML parsing bindings for lox
BuildRequires:  libyaml-devel
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description yaml
Provides the liblox_yaml.so native module for reading and parsing
YAML configuration files
%endif

# notcurses
%if %{with notcurses}
%package notcurses
Summary:        Notcurses support for the Lox programming language
BuildRequires:  notcurses-devel
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description notcurses
Provides notcurses module for slox
%endif

%package vim
Summary:        Vim support for the Lox programming language
Requires:       vim-common
BuildArch:      noarch

%description vim
Vim syntax highlighting, filetype detection, and indentation plugins
for slox (.lox) source files

# Disable standard RPM build-root policies that block installations to /usr/local
%define _unpackaged_files_terminate_build 0

%prep
%autosetup
#%setup -q

%build
# Compile both the binary target and the sub-modules via your root Makeifle
#make %{?_smp_mflags}
%set_build_flags
%make_build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_libdir}/slox/modules
mkdir -p %{buildroot}%{_libdir}/slox/lib
mkdir -p %{buildroot}%{_libdir}/slox/examples
mkdir -p %{buildroot}%{_libdir}/slox/tests
mkdir -p %{buildroot}%{_libdir}/slox/scripts
mkdir -p %{buildroot}%{_docdir}/slox/

install -m 0755 bin/slox %{buildroot}%{_bindir}/slox
install -m 0755 modules/liblox_sha1.so %{buildroot}%{_libdir}/slox/modules
install -m 0755 modules/liblox_image.so %{buildroot}%{_libdir}/slox/modules

if [ -f modules/liblox_ssl.so ]; then
    install -m 0755 modules/liblox_ssl.so %{buildroot}%{_libdir}/slox/modules/
fi

%if %{with gtk}
if [ -f modules/liblox_gi.so ]; then
    install -m 0755 modules/liblox_gi.so %{buildroot}%{_libdir}/slox/modules/
fi
%endif

%if %{with sqlite}
if [ -f modules/liblox_sqlite.so ]; then
    install -m 0755 modules/liblox_sqlite.so %{buildroot}%{_libdir}/slox/modules/
    #if [ -f modules/sqlite_ext.lox ]; then
    #    install -m 00644 modules/sqlite_ext.lox %{buildroot}%{_libdir}/slox/lib/
    #fi
fi
%endif


%if %{with postgres}
if [ -f modules/liblox_pg.so ]; then
    install -m 0755 modules/liblox_pg.so %{buildroot}%{_libdir}/slox/modules/
    #if [ -f modules/pg_ext.lox ]; then
    #    install -m 00644 modules/pg_ext.lox %{buildroot}%{_libdir}/slox/lib/
    #fi
fi
%endif

%if %{with yaml}
[ -f modules/liblox_yaml.so ] && install -m 0755 modules/liblox_yaml.so  %{buildroot}%{_libdir}/slox/modules/
%endif

%if %{with notcurses}
if [ -f modules/liblox_notcurses.so ]; then
    install -m 0755 modules/liblox_notcurses.so %{buildroot}%{_libdir}/slox/modules/
fi
%endif

cp lib/*.lox %{buildroot}%{_libdir}/slox/lib/

# Core modules (always built)
install -m 0755 modules/liblox_sha1.so %{buildroot}%{_libdir}/slox/modules/
install -m 0755 modules/liblox_image.so %{buildroot}%{_libdir}/slox/modules/
install -m 0755 modules/liblox_ffi.so %{buildroot}%{_libdir}/slox/modules/

[ -f modules/liblox_ssl.so ] && install -m 0755 modules/liblox_ssl.so %{buildroot}%{_libdir}/slox/modules/
[ -f modules/liblox_gi.so ] && install -m 0755 modules/liblox_gi.so %{buildroot}%{_libdir}/slox/modules/

# Database modules and Lox helpers
if [ -f modules/liblox_sqlite.so ]; then
    install -m 0755 modules/liblox_sqlite.so %{buildroot}%{_libdir}/slox/modules/
    [ -f modules/sqlite_ext.lox ] && install -m 0644 modules/sqlite_ext.lox %{buildroot}%{_libdir}/slox/lib/
fi


if [ -f modules/liblox_pg.so ]; then
    install -m 0755 modules/liblox_pg.so %{buildroot}%{_libdir}/slox/modules/
    [ -f modules/pg_ext.lox ] && install -m 0644 modules/pg_ext.lox %{buildroot}%{_libdir}/slox/lib/
fi
#cp modules/*.so %{buildroot}/usr/local/lib/slox/modules

cp examples/* %{buildroot}%{_libdir}/slox/examples/
cp -r tests/* %{buildroot}%{_libdir}/slox/tests/
cp scripts/* %{buildroot}%{_libdir}/slox/scripts/
cp docs/* %{buildroot}%{_docdir}/slox/

mkdir -p %{buildroot}%{_datadir}/vim/vimfiles/ftdetect
mkdir -p %{buildroot}%{_datadir}/vim/vimfiles/ftplugin
mkdir -p %{buildroot}%{_datadir}/vim/vimfiles/syntax

cp extras/lox-ftdetect.vim %{buildroot}%{_datadir}/vim/vimfiles/ftdetect/lox.vim
cp extras/lox-ftplugin.vim %{buildroot}%{_datadir}/vim/vimfiles/ftplugin/lox.vim
cp extras/lox-syntax.vim %{buildroot}%{_datadir}/vim/vimfiles/syntax/lox.vim

%clean
rm -rf %{buildroot}

%files
%{_bindir}/slox
%{_libdir}/slox/modules/liblox_sha1.so
%{_libdir}/slox/modules/liblox_image.so
%{_libdir}/slox/modules/liblox_ssl.so
%{_libdir}/slox/modules/liblox_ffi.so
%{_libdir}/slox/lib/
%{_libdir}/slox/examples/
%{_libdir}/slox/tests/
%{_libdir}/slox/scripts/
%doc README.md
%doc docs/*.md

%if %{with gtk}
%files gtk
%{_libdir}/slox/modules/liblox_gi.so
%endif

%if %{with sqlite}
%files sqlite
%{_libdir}/slox/modules/liblox_sqlite.so
%{_libdir}/slox/lib/sqlite_ext.lox
%endif

%if %{with postgres}
%files postgres
%{_libdir}/slox/modules/liblox_pg.so
%{_libdir}/slox/lib/pg_ext.lox
%endif

%if %{with yaml}
%files yaml
%{_libdir}/slox/modules/liblox_yaml.so
%endif

%if %{with notcurses}
%files notcurses
%{_libdir}/slox/modules/liblox_notcurses.so
%endif

%files vim
%{_datadir}/vim/vimfiles/ftdetect/lox.vim
%{_datadir}/vim/vimfiles/ftplugin/lox.vim
%{_datadir}/vim/vimfiles/syntax/lox.vim

%changelog
* Mon  Aug 31 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.18-1-1
- add debug to rpmbuild, fix missing gc protection

* Mon  Aug 31 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.17-1-1
- fixed static bugs, added tests

* Sun  Aug 30 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.16-1-1
- fixed logic error

* Sun  Aug 30 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.15-1-1
- added primitive types call handlers

* Sun  Aug 30 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.14-1-1
- fixed native header

* Sun  Aug 30 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.13-1-1
- refactored meta classes

* Fri  Aug 28 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.12-1-1
- added tail call super invoke optimization

* Thu  Aug 27 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.11-1-1
- added tail call optimization

* Thu  Aug 27 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.10-1-1
- fixed nested try/catch/finally

* Wed  Aug 26 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.9-1-1
- update mice code again

* Tue  Aug 25 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.8-1-1
- fixed multiffi overflow, ffi cleanup

* Tue  Aug 25 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.7-1-1
- got notcurses box working

* Tue  Aug 25 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.6-1-1
- Bump version to 1.5.5-1

* Tue  Aug 25 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.5-1-1
- added int64 type

* Tue  Aug 25 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.4-1-1
- added buffer class for opaque/unmanaged data

* Mon  Aug 24 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.3-1-1
- added ffi module

* Mon  Aug 24 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.2-1-1
- Bump version to 1.5.1-1

* Mon  Aug 24 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.1-1-1
- Bump version to 1.5.0-1

* Mon  Aug 24 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.0-1-1
- Merge branch 'ffi'

* Mon  Aug 24 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.32-1-1
- update mice code

* Sun  Aug 23 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.31-1-1
- fixed typo

* Sun  Aug 23 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.30-1-1
- add mouse support

* Sun  Aug 23 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.29-1-1
- more notcurses methods

* Wed  Aug 19 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.28-1-1
- fixed some logic errors and typos

* Wed  Aug 19 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.27-1-1
- fixed typos

* Wed  Aug 19 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.26-1-1
- added logger class

* Wed  Aug 19 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.25-1-1
- added string duplicate *

* Tue  Aug 18 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.24-1-1
- fixed logic error

* Tue  Aug 18 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.23-1-1
- added Process.popen()

* Tue  Aug 18 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.22-1-1
- Bump version to 1.4.21-1

* Tue  Aug 18 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.21-1-1
- base notcurses plus some examples

* Sun  Aug 16 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.20-1-1
- fixed binary parsing

* Fri  Aug 14 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.19-1-1
- fixed File.save and invoke_splat

* Fri  Aug 14 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.18-1-1
- fixed valueToString

* Thu  Aug 13 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.17-1-1
- moved functions into stdlib

* Thu  Aug 13 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.16-1-1
- added hot reloading

* Tue  Aug 11 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.15-1-1
- added parser and colorizer

* Mon  Aug 10 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.14-1-1
- assign if nil

* Mon  Aug 10 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.13-1-1
- added nil chaining

* Mon  Aug 10 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.12-1-1
- added nil check operator

* Wed  Aug 05 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.11-1-1
- add file.rename

* Tue  Aug 04 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.10-1-1
- add argparser options, fix array.slice

* Mon  Aug 03 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.9-1-1
- bump version is separate commit

* Mon  Aug 03 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.8-1-1
- add tags automatically

* Mon  Aug 03 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.7-1-1
- only rebuild modules if source changed

* Mon  Aug 03 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.6-1-1
- added json library and process extension

* Mon  Aug 03 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.5-1-1
- fixed typo

* Mon  Aug 03 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.4-1-1
- fixed typo

* Sun  Aug 02 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.3-1-1
- updated kitty_view

* Sat  Aug 01 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.2-3-1
- update load path for rpm libdir

* Sat  Aug 01 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.2-2-1
- more misc build changes

* Sat  Aug 01 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.2-1-1
- more misc build changes

* Sat  Aug 01 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.2-1-1
- more misc build changes

* Fri  Jul 31 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.1-2
- updated spec and Makefile

* Fri  Jul 17 2026 Lance Dillon <riffraff169@yahoo.com> - 1.4.0-1
- More features

* Fri  Jul 17 2026 Lance Dillon <riffraff169@yahoo.com> - 1.1.0-1
- Lots of tests added

* Sat  Jul 04 2026 Lance Dillon <riffraff169@yahoo.com> - 1.0.0-1
- Modularized directory layout integration
- Added cascading path lookups for native extensions and stdlib
