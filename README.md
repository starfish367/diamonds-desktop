# diamonds-desktop 💎

**diamonds-desktop** is a lightweight, modern 100% Wayland Compositor and Shell written in C11 using `wlroots`.

It features a hybrid **macOS × Windows 11 Diamonds Dock**, a sleek **Top Bar**, a floating **Start Menu**, a liquid **Control Center**, an interactive **Calendar**, advanced **Snap Assist**, and a complete native suite of lightweight desktop applications, maintaining an ultra-low RAM footprint (< 30MB) without heavy GPU gaussian blurs.

## ✨ Ecosystem Features

### 1. Wayland Compositor Core
- Built directly on `wlroots` 0.17+ and the Wayland Scene Graph API (`wlr_scene`).
- Seamless legacy X11 application compatibility via `wlr_xwayland`.
- Damage tracking & buffer management via Cairo ARGB32 shared memory surfaces.
- First-class multi-architecture support for `x86_64` and `aarch64` (ARM64 / Armbian / Raspberry Pi).
- Client-Side & Server-Side Decoration (CSD/SSD) integration with drop-shadow compensation.
- Fullscreen support with automatic dock/bar suppression (`F11`).

### 2. Top Bar (`src/topbar.c`)
- **Virtual Desktops (1..4)**: Workspace switcher with active pill highlighting (`Super+1..4`).
- **Date & 24H Clock**: Interactive trigger for the dropdown Calendar modal.
- **Control Center Status Island**: Combined Ethernet/Wi-Fi, Volume, and Battery status pill.

### 3. Diamonds Dock (`src/dock.c`)
- **Rotated Diamond Start Button**: 90-degree tilted Material vector diamond.
- **App Launchers & Indicators**: macOS-style 4px running dots & Win11-style 16x3px `#38BDF8` active window focus bars.
- **System Tray Island**: Real-time digital clock and quick actions.

### 4. Floating Start Menu (`src/startmenu.c`)
- Windows 11 style floating card triggered by the Diamond Start button (`Super` key).
- Instant search bar with vector magnifier.
- Pinned applications grid (Files, Terminal, Store, Settings, Notes, Calc, Photos, Music, Monitor, Mail, Clock).
- Quick user profile, lock screen, and shutdown actions.

### 5. Liquid Control Center (`src/control_center.c`)
- **Wired Ethernet & Wi-Fi Cards**: Real-time interface detection (`eth0`, `wlan0`), IP address reporting, and signal strength.
- **Liquid Slider Controls**: Interactive volume and brightness sliders with drag physics and percentage tooltips.
- **Quick Action Grid**: Quick toggles for Wi-Fi, Bluetooth, Dark Mode, Night Light, Airplane Mode, and Do Not Disturb.

### 6. Interactive Calendar (`src/calendar.c`)
- Monthly calendar grid with previous/next month navigation.
- Highlighted current day and event preview.

### 7. Window Management & Snap Assist (`src/snap.c`, `src/titlebar.c`)
- **Snap Assist**: Drag to top for full maximize, top-left/top-right for half-screen split, and 4 corners for quarter-screen tiling.
- **Titlebars (SSD)**: Minimize, Maximize/Restore, and Close buttons styled in dark Mica.
- **Keybindings**:
  - `Super` / `Super+S`: Toggle Start Menu
  - `Super+E`: Launch File Manager (`diamonds-files`)
  - `Super+T`: Launch Terminal (`diamonds-terminal`)
  - `Super+I`: Launch Settings (`diamonds-settings`)
  - `Super+Q` / `Alt+F4`: Close focused window
  - `Super+Down`: Minimize focused window
  - `Super+1..4`: Switch virtual desktops
  - `Alt+Tab`: Cycle through open windows
  - `F11`: Toggle fullscreen

### 8. Native Ecosystem Applications
- [**`diamonds-settings`**](src/diamonds_settings.c): Full system settings (Display, Network, Bluetooth, Personalization, Apps, Sound, Power, About).
- [**`diamonds-files`**](src/diamonds_files.c): Lightweight File Manager with sidebar, bookmark navigation, and file preview.
- [**`diamonds-terminal`**](apps/diamonds-terminal/main.c): Pure C11 Wayland terminal emulator with pseudo-terminal (PTY) support.
- [**`diamonds-notes`**](apps/diamonds-notes/main.c): Clean markdown notes editor with sidebar list and quick save.
- [**`diamonds-calc`**](apps/diamonds-calc/main.c): Modern calculator supporting basic arithmetic and percentage calculations.
- [**`diamonds-photos`**](apps/diamonds-photos/main.c): Image viewer with zoom, rotate, and gallery navigation.
- [**`diamonds-music`**](apps/diamonds-music/main.c): Media player with playlist, play/pause controls, and volume slider.
- [**`diamonds-monitor`**](apps/diamonds-monitor/main.c): Task manager and resource monitor with live CPU, RAM, and process table.
- [**`diamonds-mail`**](apps/diamonds-mail/main.c): Fast, distraction-free mail reader.
- [**`diamonds-clock`**](apps/diamonds-clock/main.c): World clock, stopwatch, and timer.
- [**`diamonds-store`**](apps/diamonds-store/main.c): Software center for installing and exploring Wayland apps.

## 🛠 Dependencies

On Debian, Ubuntu, Armbian (x86_64 and aarch64):
```bash
sudo apt update
sudo apt install -y --no-install-recommends \
  meson ninja-build pkg-config gcc \
  libwlroots-dev libwayland-dev wayland-protocols \
  libcairo2-dev libpango1.0-dev libgdk-pixbuf2.0-dev libgtk-3-dev \
  libxkbcommon-dev libpixman-1-dev libdrm-dev xwayland
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
./build/diamonds-settings
./build/diamonds-files
./build/diamonds-terminal
./build/diamonds-notes
./build/diamonds-calc
./build/diamonds-photos
./build/diamonds-music
./build/diamonds-monitor
./build/diamonds-mail
./build/diamonds-clock
./build/diamonds-store
```

## 📦 Đóng Gói APT (.deb) cho x86_64 & ARM64

Tạo file cài đặt `.deb` để cài đặt trực tiếp qua `apt`:

```bash
# Tự động nhận diện kiến trúc hiện tại (amd64 hoặc arm64) và đóng gói .deb:
./scripts/package-deb.sh 0.1.0

# Cài đặt qua APT (tự động phân giải toàn bộ dependencies):
sudo apt install ./diamonds-desktop_0.1.0_amd64.deb    # Trên x86_64
# hoặc
sudo apt install ./diamonds-desktop_0.1.0_arm64.deb    # Trên ARM64 / Armbian / RPi
```

Sau khi cài đặt xong qua APT:
- Xuất hiện tùy chọn **Diamonds Desktop** ngay tại màn hình đăng nhập (GDM / SDDM / LightDM).
- Hoặc khởi chạy từ dòng lệnh: `diamonds-session`.

## 📜 License

[MIT License](LICENSE) © 2026 Diamonds Desktop Contributors
