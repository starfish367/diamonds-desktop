project(
  'diamonds-desktop',
  'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'optimization=2',
  ],
)

cc = meson.get_compiler('c')
cpu_family = host_machine.cpu_family()
message('Target CPU family: ' + cpu_family)

# Enforce standard C11 and multi-arch safety
add_project_arguments(
  [
    '-DWLR_USE_UNSTABLE',
    '-D_POSIX_C_SOURCE=200809L',
  ],
  language: 'c',
)

# Core dependencies
wlroots = dependency('wlroots', version: '>=0.17.0')
wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols', version: '>=1.24')
cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')
xkbcommon = dependency('xkbcommon')
pixman = dependency('pixman-1')
libdrm = dependency('libdrm')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')
math_dep = cc.find_library('m', required: false)

# XWayland detection
have_xwayland = wlroots.get_variable(pkgconfig: 'have_xwayland', default_value: 'true') == 'true'
if have_xwayland
  add_project_arguments('-DDIAMONDS_ENABLE_XWAYLAND=1', language: 'c')
  message('XWayland support: ENABLED')
else
  message('XWayland support: DISABLED')
endif

# Wayland protocol code generation
wayland_scanner = find_program('wayland-scanner')
wayland_protos_dir = wayland_protos.get_variable('pkgdatadir')

xdg_shell_xml = wayland_protos_dir / 'stable/xdg-shell/xdg-shell.xml'

xdg_shell_server_h = custom_target(
  'xdg-shell-protocol-h',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

xdg_shell_client_h = custom_target(
  'xdg-shell-client-protocol-h',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

xdg_shell_c = custom_target(
  'xdg-shell-protocol-c',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

inc_dirs = include_directories('include', 'src', '.')

compositor_sources = files(
  'src/main.c',
  'src/cairo_buffer.c',
  'src/topbar.c',
  'src/dock.c',
  'src/startmenu.c',
  'src/sound.c',
  'src/snap.c',
  'src/titlebar.c',
  'src/vector_icons.c',
  'src/icon_loader.c',
  'src/wallpaper.c',
  'src/calendar.c',
  'src/control_center.c',
)

executable(
  'diamonds-desktop',
  compositor_sources,
  xdg_shell_server_h,
  xdg_shell_c,
  include_directories: inc_dirs,
  dependencies: [
    wlroots,
    wayland_server,
    cairo,
    pango,
    pangocairo,
    gdk_pixbuf,
    xkbcommon,
    pixman,
    libdrm,
    math_dep,
  ],
  install: true,
)

# Ecosystem Client Apps
app_common_sources = [
  files('apps/common/diamonds_app.c'),
  xdg_shell_client_h,
  xdg_shell_c,
]

app_deps = [
  wayland_client,
  cairo,
  pango,
  pangocairo,
  math_dep,
]

app_inc = include_directories('apps/common', '.')

executable(
  'diamonds-files',
  ['apps/diamonds-files/main.c', app_common_sources],
  include_directories: app_inc,
  dependencies: app_deps,
  install: true,
)

executable(
  'diamonds-store',
  ['apps/diamonds-store/main.c', app_common_sources],
  include_directories: app_inc,
  dependencies: app_deps,
  install: true,
)

executable(
  'diamonds-clock',
  ['apps/diamonds-clock/main.c', app_common_sources],
  include_directories: app_inc,
  dependencies: app_deps,
  install: true,
)

executable(
  'diamonds-mail',
  ['apps/diamonds-mail/main.c', app_common_sources],
  include_directories: app_inc,
  dependencies: app_deps,
  install: true,
)

executable(
  'diamonds-settings',
  ['apps/diamonds-settings/main.c', app_common_sources],
  include_directories: app_inc,
  dependencies: app_deps,
  install: true,
)
