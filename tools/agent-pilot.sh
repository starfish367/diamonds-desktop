#!/usr/bin/env bash
# tools/agent-pilot.sh: Bộ công cụ cho AI Agent nhìn và điều khiển Desktop

ACTION=$1

case "$ACTION" in
    # 1. Chụp màn hình hiện tại để Agent "nhìn"
    see)
        rm -f /tmp/diamonds_screen.png
        if [ -S /tmp/diamonds.sock ]; then
            python3 -c "import socket; s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect('/tmp/diamonds.sock'); s.sendall(b'see\n'); s.recv(10)" 2>/dev/null || true
        fi
        if [ ! -f /tmp/diamonds_screen.png ]; then
            grim /tmp/diamonds_screen.png 2>/dev/null || true
        fi
        echo "/tmp/diamonds_screen.png"
        ;;

    # 2. Click chuột vào tọa độ X Y (Ví dụ: ./agent-pilot.sh click 100 200)
    click)
        X=$2
        Y=$3
        if [ -S /tmp/diamonds.sock ]; then
            python3 -c "import socket; s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect('/tmp/diamonds.sock'); s.sendall(f'click $X $Y\n'.encode()); s.recv(10)" 2>/dev/null
            echo "Clicked ($X, $Y) via diamonds IPC"
        elif ydotool mousemove --absolute $X $Y 2>/dev/null && ydotool click 0xC0 2>/dev/null; then
            echo "Clicked ($X, $Y) via ydotool"
        else
            python3 -c "import Xlib.display, Xlib.X, time
from Xlib.ext import xtest
d = Xlib.display.Display(':0')
xtest.fake_input(d, Xlib.X.MotionNotify, x=$X, y=$Y)
d.sync()
time.sleep(0.05)
xtest.fake_input(d, Xlib.X.ButtonPress, 1)
d.sync()
time.sleep(0.05)
xtest.fake_input(d, Xlib.X.ButtonRelease, 1)
d.sync()"
            echo "Clicked ($X, $Y) via XTest fallback"
        fi
        ;;

    rclick)
        X=$2
        Y=$3
        if [ -S /tmp/diamonds.sock ]; then
            python3 -c "import socket; s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect('/tmp/diamonds.sock'); s.sendall(f'rclick $X $Y\n'.encode()); s.recv(10)" 2>/dev/null
            echo "Right-clicked ($X, $Y) via diamonds IPC"
        fi
        ;;

    # 3. Kéo thả chuột (Dùng test tính năng Snap cửa sổ Win 11)
    drag)
        X1=$2; Y1=$3; X2=$4; Y2=$5
        if ydotool mousemove --absolute $X1 $Y1 2>/dev/null && ydotool mousemove --absolute $X2 $Y2 2>/dev/null; then
            echo "Dragged via ydotool"
        else
            python3 -c "import Xlib.display, Xlib.X, time
from Xlib.ext import xtest
d = Xlib.display.Display(':0')
xtest.fake_input(d, Xlib.X.MotionNotify, x=$X1, y=$Y1)
xtest.fake_input(d, Xlib.X.ButtonPress, 1)
d.sync()
time.sleep(0.1)
xtest.fake_input(d, Xlib.X.MotionNotify, x=$X2, y=$Y2)
d.sync()
time.sleep(0.1)
xtest.fake_input(d, Xlib.X.ButtonRelease, 1)
d.sync()"
            echo "Dragged via XTest fallback"
        fi
        ;;

    # 4. Gõ phím hoặc phím tắt
    key)
        KEY=$2
        ydotool key "$KEY" 2>/dev/null || echo "Key $KEY sent"
        ;;

    type)
        TEXT=$2
        python3 -c "import socket; s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect('/tmp/diamonds.sock'); s.sendall(f'type $TEXT\n'.encode()); s.recv(10)" 2>/dev/null
        echo "Typed $TEXT"
        ;;

    startmenu)
        python3 -c "import socket; s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect('/tmp/diamonds.sock'); s.sendall(b'toggle_startmenu\n'); s.recv(10)" 2>/dev/null
        echo "Toggled Start Menu"
        ;;

    calendar)
        python3 -c "import socket; s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect('/tmp/diamonds.sock'); s.sendall(b'toggle_calendar\n'); s.recv(10)" 2>/dev/null
        echo "Toggled Calendar"
        ;;

    control_center)
        python3 -c "import socket; s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect('/tmp/diamonds.sock'); s.sendall(b'toggle_control_center\n'); s.recv(10)" 2>/dev/null
        echo "Toggled Control Center"
        ;;

    workspace)
        WS=$2
        python3 -c "import socket; s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect('/tmp/diamonds.sock'); s.sendall(f'workspace $WS\n'.encode()); s.recv(10)" 2>/dev/null
        echo "Switched to Workspace $WS"
        ;;
esac
