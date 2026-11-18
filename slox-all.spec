Name:           slox-all
Version: 1.11.3
Release: 1%{?dist}
Summary:        Metapackage for the complete Slox environment
License:        MIT
BuildArch:      noarch

# Meta-package dependencies. slox-vim is noarch; the other component
# packages are arch-specific.
Requires:       slox
Requires:       slox-gtk
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
* Wed  Nov 18 2026 Lance Dillon <riffraff169@yahoo.com> - 1.11.3-1
- Add ImageView filtering and canvas reuse hooks

* Tue Nov 17 2026 Lance Dillon <riffraff169@yahoo.com> - 1.11.2-1
- Track the slox 1.11.2 component packages.

* Thu  Aug 13 2026 Lance Dillon <riffraff169@yahoo.com> - 1.0.0-1
- initial creation

