Name:           slox-all
Version:        1.1.0
Release:        1%{?dist}
Summary:        Metapackage for the complete Slox environment
License:        MIT
BuildArch:      noarch

# Meta-package dependencies
Requires:       slox
Requires:       slox-vim
Requires:       slox-gtk
Requires:       slox-postgres
Requires:       slox-sqlite
Requires:       slox-yaml
Requires:       slox-notcurses
# Add any other subpackages here:
# Requires:       slox-stdlib
# Requires:       slox-tools

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
* Thu  Aug 13 2026 Lance Dillon <riffraff169@yahoo.com> - 1.0.0-1
- initial creation

