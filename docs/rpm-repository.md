# RPM repository layout

The Gitea build workflow publishes one repository per Fedora release and
architecture. Each job builds in a container running that Fedora release so the
packages link against the matching system libraries.

## Build images on the runner host

The workflow uses local `rpmbuild-env:fc42`, `rpmbuild-env:fc44`, and
`rpmbuild-env:fc45` images with all build requirements preinstalled. Build them
once on the runner host, and again whenever the package list changes:

```
./scripts/build-rpm-images.sh
```

To rebuild only one release, pass its number, for example
`./scripts/build-rpm-images.sh 42`.

## Server layout

The runner mounts `/opt/docker/repo/data` at `/src/rpm` in every job. Published
packages look like this:

```
/usr/share/nginx/html/
  fedora/
    42/
      x86_64/   RPMS plus repodata/
    44/
      x86_64/   RPMS plus repodata/
    45/
      x86_64/   RPMS plus repodata/
```

Noarch packages such as `slox-all` are copied into each architecture directory
so a client only needs one repository.

## nginx

The existing root server works unchanged. Autoindex is optional but useful for
browsing packages:

```
server {
    listen 80;
    server_name _;

    location / {
        root /usr/share/nginx/html;
        autoindex on;
        autoindex_exact_size off;
        autoindex_localtime on;
    }
}
```

## Client repository file

Put this in `/etc/yum.repos.d/slox-local.repo` on each Fedora system. The
`$releasever` and `$basearch` variables select the correct directory.

```
[slox-local]
name=Slox local packages
baseurl=http://YOUR-REPO-HOST/fedora/$releasever/$basearch/
enabled=1
gpgcheck=0
```

## Existing flat repository

If the old workflow already copied packages into the root of
`/opt/docker/repo/data`, move them into the matching release directory before
generating the new metadata:

```
cd /opt/docker/repo/data
for rpm in *.rpm; do
  release=$(printf '%s\n' "$rpm" | grep -o 'fc[0-9]\+' | head -1 | tr -d 'fc')
  arch=$(rpm -qp --qf '%{ARCH}\n' "$rpm")
  if [ -z "$release" ] || [ -z "$arch" ]; then
    printf 'Skipping %s\n' "$rpm"
    continue
  fi
  mkdir -p "fedora/$release/$arch"
  mv "$rpm" "fedora/$release/$arch/"
done
for repo in fedora/*/*; do
  createrepo_c --update "$repo"
done
rm -rf repodata
```

## PostgreSQL module

`slox.spec` builds `liblox_pg.so` with `BuildRequires: libpq-devel` and declares
`Requires: libpq`. That links against the stable `libpq.so.5` SONAME instead of
Fedora's version-specific private libpq SONAME, so the module is not tied to one
PostgreSQL stream.
