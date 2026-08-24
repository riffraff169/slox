#!/usr/bin/env bash
set -e

CC="${CC:-gcc}"
CFLAGS="${CFLAGS:--O2} -fPIC -shared"
LDFLAGS="${LDFLAGS}--shared"
INC="-I../src"
TARGET_DIR="${TARGET_DIR:-.}"

BUILT=()
SKIPPED=()

echo "=========================================="
echo "  Building slox C Native Modules"
echo "=========================================="

# Helper function to compile a module
compile_mod() {
    local name="$1"
    local src="$2"
    local out="$3"
    shift 3
    local flags=("$@")

    local rebuild=false
    if [ ! -f "$out" ]; then
        rebuild=true
    elif [ "$src" -nt "$out" ]; then
        rebuild=true
    else
        for header in ../src/*.h; do
            if [ "$header" -nt "$out" ]; then
                rebuild=true
                break
            fi
        done

        if [ "$rebuild" = false ] && [ -f "TrustAnchors.h" ] && [ "TrustAnchors.h" -nt "$out" ]; then
            echo rebuild 4
            rebuild=true
        fi
    fi

    if [ "$rebuild" = true ]; then
        echo "[+] Building $name ($out)..."
        if $CC $CFLAGS $INC "$src" $LDFLAGS "${flags[@]}" -o "$out"; then
            BUILT+=("$name")
        else
            echo "    [!] Failed to compile $name"
            SKIPPED+=("$name (compile error)")
        fi
    else
        echo "[-] Skipping $name (up to date)"
        SKIPPED+=("$name (up to date)")
    fi
}

# -------------------------------------------------------------------
# 1. Base Modules (Always built)
# -------------------------------------------------------------------
compile_mod "sha1"  "liblox_sha1.c"  "$TARGET_DIR/liblox_sha1.so"
compile_mod "image" "liblox_image.c" "$TARGET_DIR/liblox_image.so"

# -------------------------------------------------------------------
# 2. BearSSL / SSL Module
# -------------------------------------------------------------------
if [ -d "BearSSL/src" ]; then
    echo "[+] Building BearSSL library..."
    make -C BearSSL -s

    # Find system CA bundle
    CA_BUNDLE=""
    for path in /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem /etc/ssl/certs/ca-certificates.crt; do
        if [ -f "$path" ]; then CA_BUNDLE="$path"; break; fi
    done

    if [ -n "$CA_BUNDLE" ]; then
        gen_ta=false

        if [ ! -f "TrustAnchors.h" ]; then
            gen_ta=true
        elif [ "$CA_BUNDLE" -nt "TrustAnchors.h" ]; then
            gen_ta=true
        elif [ "liblox_ssl.c" -nt "TrustAnchors.h" ]; then
            gen_ta=true
        fi

        if [ "$gen_ta" = true ]; then
            echo "[+] Generating TrustAnchors.h from $CA_BUNDLE..."
            BearSSL/build/brssl ta "$CA_BUNDLE" > TrustAnchors.h
        else
            echo "[-] TrustAnchors.h is up to date."
        fi
    fi

    SSL_FLAGS=(-IBearSSL/inc -DHAVE_BEARSSL -Wl,--whole-archive BearSSL/build/libbearssl.a -Wl,--no-whole-archive)
    compile_mod "ssl" "liblox_ssl.c" "$TARGET_DIR/liblox_ssl.so" "${SSL_FLAGS[@]}"
else
    SKIPPED+=("ssl (BearSSL/src folder missing)")
fi

# -------------------------------------------------------------------
# 3. GTK4 / GObject Introspection Module
# -------------------------------------------------------------------
GI_PKGS="gobject-introspection-1.0 gtk4 gdk"
if pkg-config --exists $GI_PKGS 2>/dev/null; then
    GI_CFLAGS=$(pkg-config --cflags $GI_PKGS)
    GI_LIBS=$(pkg-config --libs $GI_PKGS)
    compile_mod "gi" "liblox_gi.c" "$TARGET_DIR/liblox_gi.so" $GI_CFLAGS $GI_LIBS
else
    SKIPPED+=("gi (gtk4/gobject-introspection devel packages missing)")
fi

# -------------------------------------------------------------------
# 4. SQLite Module
# -------------------------------------------------------------------
if pkg-config --exists sqlite3 2>/dev/null; then
    SQLITE_CFLAGS=$(pkg-config --cflags sqlite3)
    SQLITE_LIBS=$(pkg-config --libs sqlite3)
    compile_mod "sqlite" "liblox_sqlite.c" "$TARGET_DIR/liblox_sqlite.so" $SQLITE_CFLAGS $SQLITE_LIBS
else
    SKIPPED+=("sqlite (sqlite3-devel missing)")
fi

# -------------------------------------------------------------------
# 5. PostgreSQL Module
# -------------------------------------------------------------------
if pkg-config --exists libpq 2>/dev/null; then
    PG_CFLAGS=$(pkg-config --cflags libpq)
    PG_LIBS=$(pkg-config --libs libpq)
    compile_mod "postgres" "liblox_pg.c" "$TARGET_DIR/liblox_pg.so" $PG_CFLAGS $PG_LIBS
elif command -v pg_config >/dev/null 2>&1; then
    PG_CFLAGS="-I$(pg_config --includedir)"
    PG_LIBS="-L$(pg_config --libdir) -lpq"
    compile_mod "postgres" "liblox_pg.c" "$TARGET_DIR/liblox_pg.so" $PG_CFLAGS $PG_LIBS
else
    SKIPPED+=("postgres (postgresql-devel / libpq missing)")
fi

if pkg-config --exists yaml-0.1 2>/dev/null; then
    YAML_CFLAGS=$(pkg-config --cflags yaml-0.1)
    YAML_LIBS=$(pkg-config --libs yaml-0.1)
    compile_mod "yaml" "liblox_yaml.c" "$TARGET_DIR/liblox_yaml.so" $YAML_CFLAGS $YAML_LIBS
elif [ -f /usr/include/yaml.h ];then
    compile_mod "yaml" "liblox_yaml.c" "$TARGET_DIR/liblox_yaml.so" "-lyaml"
else
    SKIPPED+=("yaml (libyaml-devel missing)")
fi

if pkg-config --exists notcurses 2>/dev/null; then
    NOTCURSES_CFLAGS=$(pkg-config --cflags notcurses)
    NOTCURSES_LIBS=$(pkg-config --libs notcurses)
    compile_mod "notcurses"  "liblox_notcurses.c" "$TARGET_DIR/liblox_notcurses.so" $NOTCURSES_CFLAGS $NOTCURSES_LIBS
else
    SKIPPED=("notcurses (notcurses-devel missing)")
fi

if pkg-config --exists libffi 2>/dev/null; then
    LIBFFI_CFLAGS=$(pkg-config --cflags libffi)
    LIBFFI_LIBS=$(pkg-config --libs libffi)
    compile_mod "libffi" "liblox_ffi.c" "$TARGET_DIR/liblox_ffi.so" $LIBFFI_CFLAGS $LIBFFI_LIBS
else
    SKIPPED=("libffi (libffi-devel missing)")
fi

# -------------------------------------------------------------------
# Summary Output
# -------------------------------------------------------------------
echo ""
echo "=========================================="
echo "  Build Summary"
echo "=========================================="
echo "  Successfully Built: ${BUILT[*]}"
if [ ${#SKIPPED[@]} -gt 0 ]; then
    echo "  Skipped Modules:"
    for s in "${SKIPPED[@]}"; do
        echo "    - $s"
    done
fi
echo "=========================================="
