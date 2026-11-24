Name:           slox-all
Version: 1.14.0
Release: 1%{?dist}
Summary:        Metapackage for the complete Slox environment
License:        MIT
BuildArch:      noarch

# Meta-package dependencies. slox and the native module packages are
# arch-specific; the rest are noarch.
Requires:       slox
Requires:       slox-apps
Requires:       slox-canvas
Requires:       slox-examples
Requires:       slox-gtk
Requires:       slox-libs
Requires:       slox-lsp
Requires:       slox-notcurses
Requires:       slox-postgres
Requires:       slox-sqlite
Requires:       slox-vim
Requires:       slox-yaml

%description
Slox-all is a meta-package that installs the Slox runtime along with 
editor plugins, standard libraries, and associated tools.

%prep
# Nothing to prep

%build
# Nothing to build

%install
# Nothing to install

%files
# No files owned directly by metapackage

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

* Wed  Nov 18 2026 Lance Dillon <riffraff169@yahoo.com> - 1.11.4-1
- Track the new shared library, feature, app, and example packages
- Extract reusable Canvas list widget

* Wed  Nov 18 2026 Lance Dillon <riffraff169@yahoo.com> - 1.11.3-1
- Add ImageView filtering and canvas reuse hooks

* Tue Nov 17 2026 Lance Dillon <riffraff169@yahoo.com> - 1.11.2-1
- Track the slox 1.11.2 component packages.

* Thu  Aug 13 2026 Lance Dillon <riffraff169@yahoo.com> - 1.0.0-1
- initial creation

