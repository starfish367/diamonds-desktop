# diamonds-desktop 💎

**diamonds-desktop** is a lightweight, modern 100% Wayland Compositor and Shell written in C11 using `wlroots`.

It features a hybrid **macOS × Windows 11 Diamonds Dock**, a sleek **Top Bar**, a floating **Start Menu**, and a native ecosystem of lightweight desktop applications, maintaining an ultra-low RAM footprint (< 35MB) without heavy GPU gaussian blurs.

## ✨ Ecosystem Features

### 1. Wayland Compositor Core
- Built directly on `wlroots` 0.17+ and the Wayland Scene Graph API (`wlr_scene`).
- Seamless legacy X11 application compatibility via `wlr_xwayland`.
- Damage tracking & buffer management via Cairo ARGB32 shared memory surfaces.
- First-class multi-architecture support for `x86_64` and `aarch64` (ARM64 / Armbian / Raspberry Pi).

### 2. Top Bar (`src/topbar.c`)
- **Virtual Desktops (1..4)**: Workspace switcher with active pill highlighting.
- **Date & 24H Clock**: Real-time localized time and calendar display.
- **System Tray**: Background application status indicators.
- **Control Center Toggle**: Quick toggles for volume, network, and battery status.

### 3. Diamonds Dock (`src/dock.c`)
- **Rotated Diamond Start Button**: 90-degree tilted Material vector diamond.
- **App Launchers & Indicators**: macOS-style 4px running dots & Win11-style 16x3px `#38BDF8` active window focus bars.
- **Trash Can Icon**: Quick trash manager access on the right side of the dock.
- **Digital Clock Island**: High-contrast clock rendered with Cairo & Pango.

### 4. Floating Start Menu (`src/startmenu.c`)
- Windows 11 style floating card triggered by the Diamond Start button.
- Instant search bar with vector magnifier.
- Pinned applications grid (Files, Store, Clock, Mail, Terminal, Settings).
- Recommended recent files and user profile / shutdown footer.

### 5. Native Ecosystem Applications (`apps/`)
- [**`diamonds-files`**](file:///home/a/diamonds-desktop/apps/diamonds-files/main.c): Lightweight File Manager with sidebar and grid view.
- [**`diamonds-store`**](file:///home/a/diamonds-desktop/apps/diamonds-store/main.c): App Store client for installing and managing Wayland apps.
- [**`diamonds-clock`**](file:///home/a/diamonds-desktop/apps/diamonds-clock/main.c): Analog clock face with smooth second hand, digital display, and stopwatch.
- [**`diamonds-mail`**](file:///home/a/diamonds-desktop/apps/diamonds-mail/main.c): Fast, distraction-free text email reader.

## 🛠 Dependencies

On Debian/Ubuntu/Armbian:
```bash
sudo apt install meson ninja-build pkg-config gcc libwlroots-dev libwayland-dev \
  wayland-protocols libcairo2-dev libpango1.0-dev libxkbcommon-dev libdrm-dev libpixman-1-dev xwayland
```

## 🚀 Building & Running

```bash
# Configure build
meson setup build

# Compile compositor and all ecosystem apps
ninja -C build

# Run compositor (nested inside an existing Wayland or X11 session, or on DRM KMS tty)
./build/diamonds-desktop

# Run individual ecosystem apps
./build/diamonds-files
./build/diamonds-store
./build/diamonds-clock
./build/diamonds-mail
```

## 📜 License

[MIT License](LICENSE) © 2026 Diamonds Desktop Contributors
