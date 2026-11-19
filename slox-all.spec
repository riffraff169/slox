Name:           slox-all
Version: 1.12.0
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

