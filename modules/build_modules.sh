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

    echo "[+] Building $name ($out)..."
    if $CC $CFLAGS $INC "$src" $LDFLAGS "${flags[@]}" -o "$out"; then
        BUILT+=("$name")
    else
        echo "    [!] Failed to compile $name"
        SKIPPED+=("$name (compile error)")
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
        echo "[+] Generating TrustAnchors.h from $CA_BUNDLE..."
        BearSSL/build/brssl ta "$CA_BUNDLE" > TrustAnchors.h
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
