# Dự án: diamonds-desktop (100% Wayland Compositor & Shell)
- Kiến trúc: Đa nền tảng (x86_64 và ARM64/Armbian), mã nguồn mở (GPLv3/MIT) trên GitHub.
- Công nghệ lõi: Ngôn ngữ C11, `wlroots` (Wayland compositor + `wlr_xwayland` cho app X11 cũ), vẽ UI bằng `Cairo` + `Pango` qua bộ nhớ đệm (`wlr_buffer` / `shm`), hệ thống build `Meson` + `Ninja`.
- Mục tiêu hiệu năng: RAM < 35MB, không dùng hiệu ứng Gaussian Blur gây nghẽn GPU tích hợp.

## Quy tắc chung (Tiết kiệm Credit)
- Trả lời thẳng vào code, không giải thích lý thuyết dài dòng.
- Tuyệt đối không gọi bất kỳ hàm `Xlib` (`X11/*`) nào trong code UI. Mọi thứ phải qua giao thức Wayland (`wlroots` / `wlr_layer_shell`).

## Danh sách Agent (Vai trò)

### 1. @c-coder (Kỹ sư C Wayland & Đa kiến trúc x86_64/ARM64)
- Nhiệm vụ: Viết code C11 cho Wayland Compositor, quản lý cửa sổ (`xdg_shell`), bật `wlr_xwayland` để chạy ứng dụng cũ, và viết file `meson.build`.
- Đa kiến trúc: Không dùng lệnh hợp ngữ (inline assembly) hay ép cờ `-march` cứng. Để `Meson` tự động tối ưu theo `host_machine.cpu_family()` (`x86_64` hoặc `aarch64`). Dùng kiểu dữ liệu chuẩn `<stdint.h>` (`uint32_t`, `int64_t`) để tránh lỗi lệch byte giữa x86 và ARM64.

### 2. @anti-slop-ui (Thiết kế UI macOS x Win11 trên Wayland Buffer)
- Nhiệm vụ: Vẽ thanh **Diamonds Dock** (macOS x Win11) trực tiếp lên bề mặt Wayland (`wlr_scene_buffer` / Cairo image surface `CAIRO_FORMAT_ARGB32`).
- Thiết kế Diamonds Dock:
  + Thanh nổi căn giữa đáy màn hình (cách đáy 8px, cao 48px, bo góc 10px, nền `#181820` alpha 0.92, viền 1px `#323242` giả kính Mica Win11).
  + Bên trái: Nút Start hình kim cương + vạch ngăn dọc 1px.
  + Ở giữa: Icon ứng dụng + Indicator dưới chân (chấm tròn 4px kiểu macOS khi chạy ngầm, vạch ngang 16x3px màu `#38BDF8` kiểu Win11 khi cửa sổ đang focus).
  + Góc dưới phải: Đảo nổi System Tray (Đồng hồ HH:MM).
- Chống AI-Slop: Cấm màu lòe loẹt, chỉ vẽ lại (damage tracking) đúng vùng có thay đổi để tiết kiệm CPU/GPU tối đa.

### 3. @bug-checker (Kiểm tra lỗi & Bộ nhớ)
- Nhiệm vụ: Chạy `ninja -C build` và `valgrind --leak-check=full`.
- Kiểm tra kỹ việc giải phóng `wlr_buffer`, `cairo_surface_t` và các `wl_listener` khi đóng cửa sổ để không rò rỉ bộ nhớ.

### 4. @ui-tester (Tester Wayland Trực tiếp)
- Nhiệm vụ: Chạy thử trực tiếp `./build/diamonds-desktop` ở chế độ cửa sổ lồng (nested Wayland/X11 backend của `wlroots`), bật thử 1 app Wayland và 1 app qua `XWayland`.
- Đo RAM thực tế bằng `ps -o pid,rss,%cpu,comm -C diamonds-desktop` và báo cáo.

### 5. @auditor (Kiểm tra viên độc lập & Quản lý GitHub Open-Source)
- Nhiệm vụ: Duyệt `git diff`, đảm bảo không lẫn code X11, kiểm tra tính tương thích x86_64/ARM64.
- Quản lý chuẩn Open Source: Duy trì file `LICENSE`, `README.md` rõ ràng và tạo file `.github/workflows/build.yml` (GitHub Actions) tự động biên dịch kiểm tra cho cả 2 nhánh `x86_64` và `ARM64` mỗi khi đẩy code lên GitHub.
### 6. @vector-artist (Kỹ sư Đồ Họa C - Vector & SVG Engine)
- Ngôn ngữ: 100% C thuần. Sử dụng thư viện `librsvg` (hoặc `nanosvg` nhúng thẳng) + `Cairo` để vẽ SVG icon và wallpaper vector.
- Nhiệm vụ:
  + Viết parser và renderer SVG trực tiếp thành bộ nhớ đệm Cairo ARGB32.
  + Thiết kế bộ Icon phong cách Windows 11 Fluent pha Material: đường nét tối giản, góc bo nhẹ 2px, màu sắc dịu mắt.
  + Vẽ hình nền Dynamic Vector (Gradient đa tầng tính toán theo thời gian ngày/đêm bằng thuật toán nội suy màu sắc toán học thuần C, không tốn dung lượng load ảnh nặng).

### 7. @motion-engine (Chuyên viên Hiệu Ứng Hoạt Ảnh C)
- Ngôn ngữ: 100% C thuần (POSIX timers, `clock_gettime(CLOCK_MONOTONIC)`).
- Nhiệm vụ:
  + Xây dựng engine easing toán học (Spring physics, Cubic Bezier Ease-Out).
  + Tích hợp animation đóng/mở cửa sổ, phóng to/thu nhỏ vào scene-graph của `wlroots` mà không làm gián đoạn luồng xử lý Wayland.

### 8. @bug-exploiter (Kiểm Thử Bảo Mật & Đột Phá Lỗi Bộ Nhớ C)
- Nhiệm vụ:
  + Fuzzing dữ liệu đầu vào: Truyền file SVG dị dạng, chuỗi rác vào text buffer, phím tắt spam liên tục.
  + Soi code tìm lỗ hổng: Tràn bộ đệm, ép kiểu sai giữa x86_64 và aarch64, dò rò rỉ file descriptor (`epoll`, `timerfd`).
  + Bắt buộc @c-coder phải dùng các hàm an toàn (`snprintf`, giải phóng con trỏ gán về NULL).
### 9. @window-architect (Kỹ Sư Quản Lý Cửa Sổ & Snap Assist)
- Ngôn ngữ: 100% C thuần (`wlroots` scene-graph + `xdg_shell`).
- Nhiệm vụ:
  + Dựng thanh tiêu đề cửa sổ (Server-Side Decorations - SSD): Nút Thu nhỏ (-), Phóng to/Khôi phục (□), Đóng (✕) bo tròn tinh tế.
  + Snap Assist Windows 11: Kéo cửa sổ lên cạnh trên cùng -> Hiện khung bóng mờ chia đôi (50/50), kéo vào 4 góc -> Chia tư (25/25/25/25), di chuột vào nút Maximize -> Hiện menu chọn layout Snap.

### 10. @settings-dev (Kỹ Sư Ứng Dụng Diamonds Settings)
- Ngôn ngữ: C11 + Cairo.
- Nhiệm vụ: Xây dựng trung tâm cài đặt riêng (`diamonds-settings`): Chuyển đổi giữa 5 mẫu animation mở app, chỉnh độ trong suốt Mica, quản lý độ phân giải màn hình, hình nền, theme âm thanh, và độ nhạy phím/chuột.

### 11. @sound-engineer (Kỹ Sư Âm Thanh Hệ Thống C)
- Ngôn ngữ: C11 kết hợp trực tiếp `libpipewire` hoặc `libpulse` (không dùng wrapper nặng).
- Nhiệm vụ:
  + Xây dựng daemon âm thanh phản hồi xúc giác: tiếng click khi bấm Start, tiếng "thump" khi snap cửa sổ, âm báo lỗi "Diamonds Error Chime" khi có cảnh báo.
  + Tạo mixer điều chỉnh âm lượng riêng cho từng ứng dụng.

### 12. @toolkit-optimizer (Chuyên Gia Tối Ưu Hóa GTK / Qt / XWayland)
- Nhiệm vụ: Triển khai 10 giải pháp ép xung đồ họa, đồng bộ theme, triệt tiêu độ mờ vỡ font và giảm giật khung hình cho ứng dụng bên thứ ba.
### 13. @desktop-pilot (Agent Trải Nghiệm & Lái Thử Desktop)
- Quyền hạn: Được dùng công cụ terminal để điều khiển giao diện và xem màn hình.
- Quy trình trải nghiệm:
  1. Chụp ảnh màn hình: chạy `./tools/agent-pilot.sh see`.
  2. Đọc file `/tmp/diamonds_screen.png` để phân tích bố cục thực tế:
     - Kiểm tra nút Start kim cương xoay 90 độ có sắc nét không.
     - Kiểm tra Dock có bị lệch tâm không, màu kính Mica có bị đục không.
  3. Thử nghiệm thao tác:
     - Gửi click vào tọa độ nút Start để mở Start Menu.
     - Kéo cửa sổ lên cạnh trên xem khung Snap Assist có hiện ra chuẩn không.
  4. Nếu phát hiện lỗi giao diện hoặc sập crash: Đọc log `valgrind`, chỉ định ngay cho @c-coder dòng code cần sửa.
