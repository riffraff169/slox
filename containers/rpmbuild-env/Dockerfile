ARG FEDORA_VERSION=44
FROM quay.io/fedora/fedora:${FEDORA_VERSION}

# Keep this package list in sync with the BuildRequires in slox.spec and the
# modules built by modules/build_modules.sh.
RUN dnf -y install \
      git \
      rpm-build \
      rpmdevtools \
      createrepo_c \
      gcc \
      make \
      nodejs \
      ca-certificates \
      pkgconfig \
      gobject-introspection-devel \
      glib2-devel \
      gtk4-devel \
      libpq-devel \
      libyaml-devel \
      notcurses-devel \
      pcre2-devel \
      readline-devel \
      sqlite-devel \
    && dnf clean all
