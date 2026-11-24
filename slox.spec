%global _debuginfo_subpackages 1
%undefine _debugsource_packages
%global _strict_gdb_index_check 0

%bcond_without gtk
%bcond_without sqlite
%bcond_without postgres
%bcond_without yaml

Name:       slox
Version: 1.14.0
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

%global core_libs stdlib.lox mkdir_p.lox process_ext.lox stats.lox timer.lox
%global shared_libs argparser.lox benchmark.lox color.lox complex.lox csv.lox docparser.lox docparser_main.lox elf.lox event.lox json.lox lexer.lox logger.lox markdown.lox matrix.lox multiffi.lox polynomial.lox queues.lox readline.lox sfmt.lox signal-manager.lox sloxcolorizer.lox template.lox test_runner.lox uuid.lox
%global canvas_libs bmpparser.lox cairo.lox canvas.lox file_browser.lox image_view.lox kittycanvas.lox qrgen.lox x11.lox
%global lsp_libs lsp.lox
%global notcurses_libs notcurses.lox scrollbox.lox

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
Requires:       %{name}-libs = %{version}-%{release}

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
Requires:       %{name}-libs = %{version}-%{release}

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
Requires:       %{name}-libs = %{version}-%{release}

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
Requires:       %{name}-libs = %{version}-%{release}

%description yaml
Provides the liblox_yaml.so native module for reading and parsing
YAML configuration files
%endif

%package libs
Summary:        Shared pure-Lox libraries for slox
Requires:       %{name} = %{version}-%{release}
BuildArch:      noarch

%description libs
Reusable Lox libraries that are not part of the core runtime. Feature,
example, and application packages depend on this package.

%package canvas
Summary:        Cairo and X11 canvas library for slox
Requires:       %{name} = %{version}-%{release}
Requires:       %{name}-libs = %{version}-%{release}
Requires:       cairo
Requires:       libX11
Requires:       libXrandr
BuildArch:      noarch

%description canvas
Pure-Lox Cairo/X11 drawing, image view, file browser, and widget libraries.

%package lsp
Summary:        Language server library for slox
Requires:       %{name} = %{version}-%{release}
Requires:       %{name}-libs = %{version}-%{release}
BuildArch:      noarch

%description lsp
The Lox implementation of the slox language server.

%package notcurses
Summary:        Notcurses terminal UI library for slox
Requires:       %{name} = %{version}-%{release}
Requires:       %{name}-libs = %{version}-%{release}
Requires:       notcurses
BuildArch:      noarch

%description notcurses
Pure-Lox Notcurses bindings and terminal widgets for slox.

%package apps
Summary:        Command line applications for slox
Requires:       %{name} = %{version}-%{release}
Requires:       %{name}-libs = %{version}-%{release}
Requires:       %{name}-canvas = %{version}-%{release}
Requires:       %{name}-notcurses = %{version}-%{release}
BuildArch:      noarch

%description apps
Curated slox applications with prefixed commands in %{_bindir}.

%package examples
Summary:        Example programs for slox
Requires:       %{name} = %{version}-%{release}
Requires:       %{name}-libs = %{version}-%{release}
BuildArch:      noarch

%description examples
Language examples and small demos installed under %{_datadir}/slox/examples.

%package vim
Summary:        Vim support for the Lox programming language
Requires:       vim-common
Requires:       %{name}-libs = %{version}-%{release}
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
export CFLAGS="%{optflags} -g"
make %{?_smp_mflags}
#%make_build

# Generate API documentation with the freshly built interpreter
mkdir -p docs
./bin/slox lib/docparser_main.lox lib/*.lox src/native.c src/signals.c -o docs/api_doc.md

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_libdir}/slox/modules
mkdir -p %{buildroot}%{_libdir}/slox/lib
mkdir -p %{buildroot}%{_libdir}/slox/tests
mkdir -p %{buildroot}%{_datadir}/slox/examples
mkdir -p %{buildroot}%{_datadir}/slox/apps
mkdir -p %{buildroot}%{_docdir}/slox/

install -m 0775 bin/slox %{buildroot}%{_bindir}/slox
install -m 0775 modules/liblox_sha1.so %{buildroot}%{_libdir}/slox/modules/
install -m 0775 modules/liblox_image.so %{buildroot}%{_libdir}/slox/modules/
install -m 0775 modules/liblox_ffi.so %{buildroot}%{_libdir}/slox/modules/

if [ -f modules/liblox_ssl.so ]; then
    install -m 0775 modules/liblox_ssl.so %{buildroot}%{_libdir}/slox/modules/
fi

for lib in %{core_libs}; do
    install -m 0644 lib/$lib %{buildroot}%{_libdir}/slox/lib/
done

for lib in %{shared_libs}; do
    install -m 0644 lib/$lib %{buildroot}%{_libdir}/slox/lib/
done

for lib in %{canvas_libs}; do
    install -m 0644 lib/$lib %{buildroot}%{_libdir}/slox/lib/
done

for lib in %{lsp_libs}; do
    install -m 0644 lib/$lib %{buildroot}%{_libdir}/slox/lib/
done

for lib in %{notcurses_libs}; do
    install -m 0644 lib/$lib %{buildroot}%{_libdir}/slox/lib/
done

%if %{with gtk}
if [ -f modules/liblox_gi.so ]; then
    install -m 0775 modules/liblox_gi.so %{buildroot}%{_libdir}/slox/modules/
fi
%endif

%if %{with sqlite}
if [ -f modules/liblox_sqlite.so ]; then
    install -m 0775 modules/liblox_sqlite.so %{buildroot}%{_libdir}/slox/modules/
    install -m 0644 lib/sqlite_ext.lox %{buildroot}%{_libdir}/slox/lib/
fi
%endif

%if %{with postgres}
if [ -f modules/liblox_pg.so ]; then
    install -m 0775 modules/liblox_pg.so %{buildroot}%{_libdir}/slox/modules/
    install -m 0644 lib/pg_ext.lox %{buildroot}%{_libdir}/slox/lib/
fi
%endif

%if %{with yaml}
[ -f modules/liblox_yaml.so ] && install -m 0775 modules/liblox_yaml.so %{buildroot}%{_libdir}/slox/modules/
%endif

cp examples/* %{buildroot}%{_datadir}/slox/examples/
cp apps/*.lox %{buildroot}%{_datadir}/slox/apps/
cp -r tests/* %{buildroot}%{_libdir}/slox/tests/
cp docs/* %{buildroot}%{_docdir}/slox/

_slox_wrapper() {
    cat > "%{buildroot}%{_bindir}/$1" <<EOF
#!/bin/sh
exec %{_bindir}/slox "$2" "\$@"
EOF
    chmod 0755 "%{buildroot}%{_bindir}/$1"
}

_slox_wrapper slox-xxd %{_datadir}/slox/apps/xxd.lox
_slox_wrapper slox-elf-info %{_datadir}/slox/apps/elf_info.lox
_slox_wrapper slox-sanitize %{_datadir}/slox/apps/sanitize.lox
_slox_wrapper slox-image-viewer %{_datadir}/slox/apps/image_viewer.lox
_slox_wrapper slox-kitty-view %{_datadir}/slox/apps/kitty_view.lox
_slox_wrapper slox-plotcalc %{_datadir}/slox/apps/plotcalc.lox
_slox_wrapper slox-playlist-manager %{_datadir}/slox/apps/playlist_manager.lox
_slox_wrapper slox-mdcat %{_datadir}/slox/apps/mdcat.lox
_slox_wrapper slox-docgen %{_libdir}/slox/lib/docparser_main.lox

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
%{_libdir}/slox/lib/stdlib.lox
%{_libdir}/slox/lib/mkdir_p.lox
%{_libdir}/slox/lib/process_ext.lox
%{_libdir}/slox/lib/stats.lox
%{_libdir}/slox/lib/timer.lox
%{_libdir}/slox/tests/
%doc README.md
%doc docs/*.md

%files libs
%{_libdir}/slox/lib/argparser.lox
%{_libdir}/slox/lib/benchmark.lox
%{_libdir}/slox/lib/color.lox
%{_libdir}/slox/lib/complex.lox
%{_libdir}/slox/lib/csv.lox
%{_libdir}/slox/lib/docparser.lox
%{_libdir}/slox/lib/docparser_main.lox
%{_libdir}/slox/lib/elf.lox
%{_libdir}/slox/lib/event.lox
%{_libdir}/slox/lib/json.lox
%{_libdir}/slox/lib/lexer.lox
%{_libdir}/slox/lib/logger.lox
%{_libdir}/slox/lib/markdown.lox
%{_libdir}/slox/lib/matrix.lox
%{_libdir}/slox/lib/multiffi.lox
%{_libdir}/slox/lib/polynomial.lox
%{_libdir}/slox/lib/queues.lox
%{_libdir}/slox/lib/readline.lox
%{_libdir}/slox/lib/sfmt.lox
%{_libdir}/slox/lib/signal-manager.lox
%{_libdir}/slox/lib/sloxcolorizer.lox
%{_libdir}/slox/lib/template.lox
%{_libdir}/slox/lib/test_runner.lox
%{_libdir}/slox/lib/uuid.lox

%files canvas
%{_libdir}/slox/lib/bmpparser.lox
%{_libdir}/slox/lib/cairo.lox
%{_libdir}/slox/lib/canvas.lox
%{_libdir}/slox/lib/file_browser.lox
%{_libdir}/slox/lib/image_view.lox
%{_libdir}/slox/lib/kittycanvas.lox
%{_libdir}/slox/lib/qrgen.lox
%{_libdir}/slox/lib/x11.lox

%files lsp
%{_libdir}/slox/lib/lsp.lox

%files notcurses
%{_libdir}/slox/lib/notcurses.lox
%{_libdir}/slox/lib/scrollbox.lox

%files apps
%{_bindir}/slox-xxd
%{_bindir}/slox-elf-info
%{_bindir}/slox-sanitize
%{_bindir}/slox-image-viewer
%{_bindir}/slox-kitty-view
%{_bindir}/slox-plotcalc
%{_bindir}/slox-playlist-manager
%{_bindir}/slox-mdcat
%{_bindir}/slox-docgen
%{_datadir}/slox/apps/

%files examples
%{_datadir}/slox/examples/

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

%files vim
%{_datadir}/vim/vimfiles/ftdetect/lox.vim
%{_datadir}/vim/vimfiles/ftplugin/lox.vim
%{_datadir}/vim/vimfiles/syntax/lox.vim

%changelog
* Tue  Nov 24 2026 Lance Dillon <riffraff169@yahoo.com> - 1.14.0-1
- Merge pull request 'Add class constants as parameter defaults' (#28) from add/class-constant-defaults into master
- Merge pull request 'Fix kitty_view terminal size parsing' (#30) from fix/kitty-view-terminal-size into master

* Mon  Nov 23 2026 Lance Dillon <riffraff169@yahoo.com> - 1.13.0-1
- Merge pull request 'Migrate load type errors' (#25) from migrate-load-type-errors into master

* Mon  Nov 23 2026 Lance Dillon <riffraff169@yahoo.com> - 1.12.2-1
- Merge pull request 'Fix/value map keys' (#22) from fix/value-map-keys into master

* Mon  Nov 23 2026 Lance Dillon <riffraff169@yahoo.com> - 1.12.1-1
- Merge pull request 'Make interpolation convert any value with valueToString' (#18) from fix/interpolation-value-conversion into master

* Thu  Nov 19 2026 Lance Dillon <riffraff169@yahoo.com> - 1.12.0-1
- Merge pull request 'Split RPM packages by library, feature, app, and example roles' (#3) from fix/rpm-package-layout into master
- Stop installing maintainer scripts in RPM packages

* Wed  Nov 18 2026 Lance Dillon <riffraff169@yahoo.com> - 1.11.4-1
- Split libraries, examples, apps, and feature packs into subpackages
- Extract reusable Canvas list widget

* Wed  Nov 18 2026 Lance Dillon <riffraff169@yahoo.com> - 1.11.3-1
- Add ImageView filtering and canvas reuse hooks

* Mon  Nov 16 2026 Lance Dillon <riffraff169@yahoo.com> - 1.11.2-1-1
- Replace eval-based JSON parser with strict parser

* Sun  Nov 15 2026 Lance Dillon <riffraff169@yahoo.com> - 1.11.1-1-1
- Add X11 + Cairo canvas library with plotting and widgets

* Sun  Nov 15 2026 Lance Dillon <riffraff169@yahoo.com> - 1.11.0-1-1
- Normalize indentation in test_color1 example

* Thu  Nov 12 2026 Lance Dillon <riffraff169@yahoo.com> - 1.10.10-1-1
- Propagate exceptions thrown by collection callbacks

* Wed  Nov 11 2026 Lance Dillon <riffraff169@yahoo.com> - 1.10.9-1-1
- Fix break/continue and return unwinding through try blocks

* Wed  Nov 11 2026 Lance Dillon <riffraff169@yahoo.com> - 1.10.8-1-1
- Bump version to 1.10.7-1

* Wed  Nov 11 2026 Lance Dillon <riffraff169@yahoo.com> - 1.10.7-1-1
- Reject invalid control flow in defer bodies

* Wed  Nov 11 2026 Lance Dillon <riffraff169@yahoo.com> - 1.10.6-1-1
- Make compiler locals and upvalues dynamically allocated

* Wed  Nov 11 2026 Lance Dillon <riffraff169@yahoo.com> - 1.10.5-1-1
- fixed foreach and a few other fixes

* Tue  Nov 10 2026 Lance Dillon <riffraff169@yahoo.com> - 1.10.4-1-1
- added length_utf8()

* Mon  Nov 09 2026 Lance Dillon <riffraff169@yahoo.com> - 1.10.3-1-1
- more fixes and tests

* Fri  Nov 06 2026 Lance Dillon <riffraff169@yahoo.com> - 1.10.2-1-1
- added string.replace

* Fri  Nov 06 2026 Lance Dillon <riffraff169@yahoo.com> - 1.10.1-1-1
- Bump version to 1.8.11-1

* Fri  Nov 06 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.11-1-1
- fixed typo in json

* Fri  Nov 06 2026 Lance Dillon <riffraff169@yahoo.com> - 1.10.0-1-1
- centralize version and generate API docs during RPM build

* Thu  Nov 05 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.10-1-1
- added more tests, fixed some bugs

* Sat  Oct 31 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.9-1-1
- fixed default args for methods

* Sat  Oct 31 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.8-1-1
- added uuid class

* Mon  Oct 26 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.7-1-1
- added index test

* Sat  Oct 24 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.6-1-1
- added octal and hex code embedding in strings

* Wed  Oct 21 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.5-1-1
- finished defer

* Thu  Oct 15 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.4-1-1
- Bump version to 1.8.3-1

* Thu  Oct 15 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.3-1-1
- only build on tag

* Wed  Oct 14 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.2-1-1
- added some api docs, modified gc api

* Sat  Oct 10 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.1-1-1
- fixed hashtable bug

* Thu  Oct 08 2026 Lance Dillon <riffraff169@yahoo.com> - 1.8.0-1-1
- experimental qrcode generator

* Mon  Oct 05 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.14-1-1
- added markdown generator, markdown terminal printer, some code fixes

* Thu  Oct 01 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.13-1-1
- adding scrollbox, system stats

* Fri  Sep 25 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.12-1-1
- more lsp fixes and updates

* Thu  Sep 24 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.11-1-1
- added a couple Dir functions, more lsp work

* Wed  Sep 23 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.10-1-1
- more lsp features, updated vim lsp info

* Wed  Sep 23 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.9-1-1
- fixed try/catch, eval, added base lsp

* Wed  Sep 23 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.8-1-1
- fix throw in eval

* Tue  Sep 22 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.7-1-1
- added overload for comparison operators

* Tue  Sep 22 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.6-1-1
- more features with calc plot

* Sat  Sep 19 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.5-1-1
- fixed stack underflow bug

* Tue  Sep 15 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.4-1-1
- fix int/float issues

* Mon  Sep 14 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.3-1-1
- fixed float parsing issues

* Sun  Sep 13 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.2-1-1
- separated float from number

* Sat  Sep 12 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.1-1-1
- fix pack/unpack regression

* Sat  Sep 12 2026 Lance Dillon <riffraff169@yahoo.com> - 1.7.0-1-1
- Bump version to 1.6.6-1

* Sat  Sep 12 2026 Lance Dillon <riffraff169@yahoo.com> - 1.6.6-1-1
- final integer implementation...all tests passed and ffi working

* Tue  Sep 08 2026 Lance Dillon <riffraff169@yahoo.com> - 1.6.5-1-1
- Bump version to 1.6.4-1

* Tue  Sep 08 2026 Lance Dillon <riffraff169@yahoo.com> - 1.6.4-1-1
- minor comments

* Tue  Sep 08 2026 Lance Dillon <riffraff169@yahoo.com> - 1.6.3-1-1
- bug on ifdef'ed out code

* Thu  Sep 03 2026 Lance Dillon <riffraff169@yahoo.com> - 1.6.2-1-1
- added timers, refactored class anchoring

* Wed  Sep 02 2026 Lance Dillon <riffraff169@yahoo.com> - 1.6.1-1-1
- updated string.format(), added more examples and library files

* Tue  Sep 01 2026 Lance Dillon <riffraff169@yahoo.com> - 1.6.0-1-1
- fixed signals

* Tue  Sep 01 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.21-1-1
- fixed operator overloading

* Tue  Sep 01 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.20-1-1
- fixed regression

* Mon  Aug 31 2026 Lance Dillon <riffraff169@yahoo.com> - 1.5.19-1-1
- added debug symbols

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
