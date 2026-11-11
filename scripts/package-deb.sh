#!/usr/bin/env bash
# package-deb.sh: Build a production Debian package (.deb) for diamonds-desktop
# Supports both x86_64 (amd64) and ARM64 (arm64 / Armbian / Raspberry Pi)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"

VERSION="${1:-0.1.0}"
ARCH="${ARCH:-$(dpkg --print-architecture 2>/dev/null || uname -m)}"

# Normalize architecture name to Debian standard
case "$ARCH" in
    x86_64|amd64)
        ARCH="amd64"
        ;;
    aarch64|arm64)
        ARCH="arm64"
        ;;
    armv7l|armhf)
        ARCH="armhf"
        ;;
    *)
        echo "[ERROR] Unsupported architecture: $ARCH" >&2
        exit 1
        ;;
esac

echo "=========================================================="
echo " Packaging diamonds-desktop for APT ($ARCH)"
echo " Version: $VERSION"
echo "=========================================================="

BUILD_DIR="$REPO_ROOT/build-deb-$ARCH"
STAGING_DIR="$REPO_ROOT/pkg-staging-$ARCH"
PKG_NAME="diamonds-desktop_${VERSION}_${ARCH}"
OUTPUT_DEB="$REPO_ROOT/${PKG_NAME}.deb"

# 1. Configure and build with prefix=/usr
echo "==> Configuring Meson build (prefix=/usr)..."
if [ -d "$BUILD_DIR" ]; then
    meson setup "$BUILD_DIR" --prefix=/usr --reconfigure -Dwarning_level=2 -Doptimization=2
else
    meson setup "$BUILD_DIR" --prefix=/usr -Dwarning_level=2 -Doptimization=2
fi

echo "==> Compiling with Ninja..."
ninja -C "$BUILD_DIR"

# 2. Stage installation into STAGING_DIR
echo "==> Staging files with DESTDIR..."
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"
DESTDIR="$STAGING_DIR" ninja -C "$BUILD_DIR" install

# 3. Strip binaries to reduce size and RAM footprint
echo "==> Stripping binaries..."
find "$STAGING_DIR/usr/bin" -type f -exec file {} + | grep -i elf | cut -d: -f1 | xargs -r strip --strip-unneeded || true

# 4. Create DEBIAN metadata directory
mkdir -p "$STAGING_DIR/DEBIAN"

# Calculate installed size in KB
INSTALLED_SIZE=$(du -sk "$STAGING_DIR" | awk '{print $1}')

cat <<EOF > "$STAGING_DIR/DEBIAN/control"
Package: diamonds-desktop
Version: ${VERSION}
Section: x11
Priority: optional
Architecture: ${ARCH}
Maintainer: starfish367 <starfish367@users.noreply.github.com>
Installed-Size: ${INSTALLED_SIZE}
Depends: libc6, libwlroots12t64 | libwlroots-dev, libcairo2, libpango-1.0-0, libpangocairo-1.0-0, libgtk-3-0t64 | libgtk-3-0, libgdk-pixbuf-2.0-0, libxkbcommon0, libpixman-1-0, libdrm2, xwayland
Recommends: fonts-dejavu-core, pulseaudio | pipewire-pulse, foot | alacritty
Homepage: https://github.com/starfish367/diamonds-desktop
Description: Lightweight 100% Wayland Compositor and Shell (macOS x Win11)
 diamonds-desktop is a fast, ultra-low memory (< 30MB) Wayland Compositor
 and Desktop Shell built on C11 and wlroots. It features a hybrid macOS x Win11
 Diamonds Dock, sleek Top Bar, floating Start Menu, liquid Control Center,
 Snap Assist, and a full suite of native desktop applications.
EOF

# 5. Build Debian package using dpkg-deb
echo "==> Building .deb package..."
dpkg-deb --build --root-owner-group "$STAGING_DIR" "$OUTPUT_DEB"

# Clean up staging directory
rm -rf "$STAGING_DIR"

echo "=========================================================="
echo " [SUCCESS] Package created successfully!"
echo " Path: $OUTPUT_DEB"
echo " Size: $(du -h "$OUTPUT_DEB" | awk '{print $1}')"
echo " Install via APT: sudo apt install ./${PKG_NAME}.deb"
echo "=========================================================="
