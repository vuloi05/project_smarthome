#!/usr/bin/env bash
# ==============================================================================
# Script quan ly chay / dung cac thiet bi dien mo phong (Dev & Testing)
# ==============================================================================

PID_FILE="/tmp/smarthome_devices.pids"
BIN_DIR="./bin"
DEVICE_BIN="$BIN_DIR/device_server"

build_if_needed() {
    if [ ! -f "$DEVICE_BIN" ]; then
        echo "[INFO] Khong tim thay $DEVICE_BIN, dang tien hanh bien dich..."
        mkdir -p "$BIN_DIR"
        gcc -Wall -Wextra -g -Icommon device_sim/device_server.c common/net_utils.c -o "$DEVICE_BIN"
        if [ $? -ne 0 ]; then
            echo "[ERROR] Bien dich device_server that bai!"
            exit 1
        fi
        echo "[INFO] Bien dich thanh cong!"
    fi
}

start_devices() {
    if [ -f "$PID_FILE" ]; then
        echo "[WARN] Thiet bi mo phong co the dang chay roi. Chay './scripts/run_sim_devices.sh stop' truoc neu muon khoi dong lai."
        return
    fi

    build_if_needed

    echo "[INFO] Dang khoi dong 3 thiet bi mo phong..."
    
    # 1. Den phong khach (Port 8001)
    "$DEVICE_BIN" LIGHT01 LIGHT 8001 123456 Den_Phong_Khach > /dev/null 2>&1 &
    PID_LIGHT=$!

    # 2. Quat phong ngu (Port 8002)
    "$DEVICE_BIN" FAN01 FAN 8002 123456 Quat_Phong_Ngu > /dev/null 2>&1 &
    PID_FAN=$!

    # 3. Dieu hoa phong khach (Port 8003)
    "$DEVICE_BIN" AC01 AC 8003 123456 Dieu_Hoa_Phong_Khach > /dev/null 2>&1 &
    PID_AC=$!

    echo "$PID_LIGHT $PID_FAN $PID_AC" > "$PID_FILE"

    echo "[OK] Da khoi dong cac thiet bi:"
    echo "  - Den phong khach:     ID=LIGHT01, Port=8001, PID=$PID_LIGHT"
    echo "  - Quat phong ngu:      ID=FAN01,   Port=8002, PID=$PID_FAN"
    echo "  - Dieu hoa phong khach: ID=AC01,    Port=8003, PID=$PID_AC"
    echo "[INFO] San sang nhan lenh quet UDP 9999 va ket noi TCP."
}

stop_devices() {
    if [ ! -f "$PID_FILE" ]; then
        echo "[INFO] Khong co thiet bi nao dang chay (khong tim thay $PID_FILE)."
        return
    fi

    echo "[INFO] Dang gui tin hieu SIGTERM de dung an toan cac thiet bi mo phong..."
    PIDS=$(cat "$PID_FILE")
    for pid in $PIDS; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -15 "$pid" 2>/dev/null
            echo "  - Da gui tin hieu dung toi PID $pid"
        fi
    done

    rm -f "$PID_FILE"
    echo "[OK] Da tat toan bo thiet bi mo phong."
}

status_devices() {
    if [ ! -f "$PID_FILE" ]; then
        echo "[STATUS] Tat ca thiet bi mo phong dang DUNG."
        return
    fi

    echo "[STATUS] Kiem tra trang thai tien trinh:"
    PIDS=$(cat "$PID_FILE")
    for pid in $PIDS; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "  - PID $pid: DANG CHAY"
        else
            echo "  - PID $pid: DA DUNG"
        fi
    done
}

case "$1" in
    start)
        start_devices
        ;;
    stop)
        stop_devices
        ;;
    status)
        status_devices
        ;;
    restart)
        stop_devices
        sleep 1
        start_devices
        ;;
    *)
        echo "Cach dung: $0 {start|stop|status|restart}"
        exit 1
        ;;
esac
