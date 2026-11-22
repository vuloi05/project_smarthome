# Hệ Thống Điều Khiển Thiết Bị Điện Thông Minh (IT4062 - Smart Home)

[![Smart Home CI](https://github.com/vuloi05/project_smarthome/actions/workflows/ci.yml/badge.svg)](https://github.com/vuloi05/project_smarthome/actions/workflows/ci.yml)

Đồ án học phần **IT4062 — Thực hành Lập trình mạng** (Trường CNTT&TT - Đại học Bách khoa Hà Nội).  
Đề tài 11: **Xây dựng ứng dụng điều khiển thiết bị điện (Mô phỏng trên Linux C & Phần cứng thực tế ESP32)**.

---

## 1. Cấu trúc Dự án

```text
project_smarthome/
├── common/
│   ├── protocol.h          # Đặc tả giao thức tầng ứng dụng, mã trạng thái, công suất
│   ├── net_utils.h         # Khai báo các hàm xử lý truyền dòng, sinh token, tính điện năng
│   └── net_utils.c         # Cài đặt recv_line(), send_line(), generate_random_hex(), write_log()
├── device_sim/
│   └── device_server.c     # Chương trình mô phỏng Thiết bị trên Linux (dùng poll(), UDP + TCP)
├── client_app/
│   ├── home_manager.h      # Cấu trúc dữ liệu phân cấp: Căn nhà -> Phòng -> Thiết bị
│   ├── home_manager.c      # Các hàm quản lý CRUD và lưu trữ bền vững (client_data.db)
│   └── user_client.c       # Ứng dụng điều khiển cho người dùng (Menu CLI, Quét UDP, Điều khiển TCP)
├── esp32_firmware/
│   ├── platformio.ini      # Cấu hình PlatformIO cho mạch DOIT ESP32 DEVKIT V1 (CP2102)
│   └── src/
│       └── main.cpp        # Firmware cho vi điều khiển ESP32 (Wi-Fi, UDP 9999, TCP 8001, PWM LED/Fan)
├── Makefile                # Tự động biên dịch toàn bộ chương trình Linux C
└── README.md               # Tài liệu hướng dẫn sử dụng và đặc tả giao thức
```

---

## 2. Đặc tả Giao thức Tầng Ứng dụng (Custom Protocol)

### 2.1. Giao thức Quét Thiết bị (UDP Broadcast - Cổng `9999`)
- **Client phát quảng bá:** `SCAN_REQ\r\n`
- **Thiết bị phản hồi:** `SCAN_RES|<dev_id>|<type>|<tcp_port>|<dev_name>\r\n`

### 2.2. Giao thức Điều khiển & Xác thực (TCP - Truyền dòng kết thúc bởi `\r\n`)

| Chức năng | Bản tin Yêu cầu từ Client (`\r\n`) | Bản tin Phản hồi từ Thiết bị (`\r\n`) |
| :--- | :--- | :--- |
| **Xác thực kết nối** | `AUTH\|<app_id>\|<password>` | `100\|AUTH_OK\|<token>\|<id>\|<type>\|<name>\|<power>\|<watt>`<br/>hoặc `201\|WRONG_PASSWORD` |
| **Đổi mật khẩu** | `PASSWD\|<token>\|<old_pass>\|<new_pass>` | `100\|PASSWD_OK` hoặc `202\|WRONG_OLD_PASSWORD` |
| **Bật / Tắt** | `SET_POWER\|<token>\|<ON/OFF>` | `100\|POWER_OK\|<ON/OFF>\|<watt>` |
| **Hẹn giờ bật/tắt** | `SET_TIMER\|<token>\|<seconds>\|<ON/OFF>` | `100\|TIMER_OK\|<seconds>\|<ON/OFF>` |
| **Tốc độ gió Quạt** | `SET_FAN\|<token>\|<1/2/3>` | `100\|FAN_OK\|<speed>\|<watt>` |
| **Chế độ Điều hòa** | `SET_AC\|<token>\|<COOL/HEAT/DRY>\|<18..30>` | `100\|AC_OK\|<mode>\|<temp>\|<watt>` |
| **Xem thông tin** | `GET_INFO\|<token>` | `100\|INFO\|<id>\|<type>\|<name>\|<power>\|<watt>\|<fan>\|<mode>\|<temp>\|<tmr_rem>\|<tmr_act>\|<day_s>\|<month_s>\|<year_s>\|<wh>` |

---

## 3. Hướng dẫn Biên dịch và Chạy trên Linux / WSL

### Bước 1: Biên dịch
```bash
cd project_smarthome
make
```
Sau khi biên dịch, 2 file thực thi sẽ nằm trong thư mục `bin/`:
- `bin/device_server`: Chương trình mô phỏng thiết bị điện.
- `bin/user_client`: Chương trình điều khiển cho người dùng.

### Bước 2: Khởi chạy các Thiết bị mô phỏng (Mở các tab Terminal riêng hoặc chạy ngầm)
```bash
# Tab 1: Khởi chạy Bóng đèn phòng khách (TCP Port 8001, mật khẩu: 123456)
./bin/device_server LIGHT01 LIGHT 8001 123456 Den_Phong_Khach

# Tab 2: Khởi chạy Quạt điện phòng ngủ (TCP Port 8002, mật khẩu: 123456)
./bin/device_server FAN01 FAN 8002 123456 Quat_Phong_Ngu

# Tab 3: Khởi chạy Điều hòa phòng khách (TCP Port 8003, mật khẩu: 123456)
./bin/device_server AC01 AC 8003 123456 Dieu_Hoa_Phong_Khach
```

### Bước 3: Khởi chạy Chương trình Điều khiển cho Người dùng
```bash
./bin/user_client
```
1. Chọn **`4. Quét tìm thiết bị trên mạng`** $\rightarrow$ Nhấn `Enter` để tự động tìm toàn bộ các thiết bị đang chạy và thêm vào Phòng.
2. Chọn **`5. Kết nối & Điều khiển Thiết bị`** $\rightarrow$ Nhập mã thiết bị (VD: `LIGHT01`, `FAN01`, `AC01` hoặc `ESP32_DEV01`) và nhập mật khẩu `123456`.

---

## 4. Hướng dẫn Nạp Firmware lên Mạch Phần cứng ESP32 (PlatformIO)

### Phần cứng hỗ trợ
- Bo mạch: **DOIT ESP32 DEVKIT V1** / **NodeMCU-32S** (Chip `ESP-WROOM-32`, giao tiếp USB-UART `CP2102`).
- **Đèn LED tích hợp (`GPIO 2`)**: Sáng/tắt theo lệnh Bật/Tắt và thay đổi độ sáng PWM theo tốc độ gió quạt (Mức 1, 2, 3) hoặc công suất điều hòa.
- **Nút nhấn vật lý `BOOT` (`GPIO 0`)**: Bấm trực tiếp nút `BOOT` trên mạch để đảo trạng thái Bật/Tắt tại chỗ.

### Cách nạp và cấu hình Wi-Fi nhanh
1. Mở thư mục `project_smarthome/esp32_firmware` trong **VS Code (PlatformIO)**.
2. Cắm cáp USB kết nối mạch ESP32 vào máy tính, bấm **PlatformIO: Upload** và mở **Serial Monitor (115200 baud)**.
3. Trong cửa sổ Serial Monitor, bạn có thể đổi tên Wi-Fi hoặc đổi loại thiết bị bất kỳ lúc nào mà **không cần nạp lại code**:
   - Đổi Wi-Fi:
     ```text
     WIFI|Ten_WiFi_Cua_Ban|Mat_Khau_WiFi
     ```
   - Đổi vai trò thiết bị (Bóng đèn / Quạt / Điều hòa):
     ```text
     TYPE|LIGHT|ESP32_LIGHT|Den_Ban_ESP32
     TYPE|FAN|ESP32_FAN|Quat_ESP32
     TYPE|AC|ESP32_AC|Dieu_Hoa_ESP32
     ```
   - Xem địa chỉ IP hiện tại của mạch ESP32:
     ```text
     STATUS
     ```
4. Khi mạch ESP32 đã kết nối Wi-Fi và in ra địa chỉ IP (ví dụ `192.168.1.50`), trong chương trình `./bin/user_client`, bạn vào mục **`4. Quét tìm thiết bị`** và nhập địa chỉ IP `192.168.1.50` (hoặc địa chỉ Broadcast của mạng Wi-Fi như `192.168.1.255`), ứng dụng sẽ phát hiện ra ngay mạch ESP32 và điều khiển phần cứng theo thời gian thực!

---

## 5. Kiểm thử Giao thức Mạng Tự động (Automated Testing & CI/CD)

Hệ thống tích hợp sẵn bộ kiểm thử giao thức tự động chuẩn hóa bằng Python (`scripts/test_protocol.py`) và luồng GitHub Actions CI (`.github/workflows/ci.yml`).

### Chạy test tự động với Makefile
```bash
make test
```
Lệnh này sẽ tự động:
1. Biên dịch toàn bộ các ứng dụng C (`device_server`, `user_client`, `web_dashboard`).
2. Tự động spawn thiết bị mô phỏng tạm thời và kiểm thử toàn diện các luồng mạng:
   - **UDP Broadcast Discovery**: Quét thiết bị mạng qua `SCAN_REQ` $\rightarrow$ `SCAN_RES`.
   - **TCP Authentication**: Kiểm tra mã `201|WRONG_PASSWORD` và xác thực thành công `100|AUTH_OK` cấp Token.
   - **Bảo mật & Phân quyền**: Ngăn chặn truy cập trái phép bằng Token sai `203|UNAUTHORIZED`.
   - **Điều khiển thiết bị**: Bật/Tắt `SET_POWER`, truy vấn trạng thái `GET_INFO`, hẹn giờ `SET_TIMER`.
   - **Kiểm tra tham số**: Xử lý lỗi dữ liệu không hợp lệ `204|INVALID_PARAM`.
3. Tắt tiến trình an toàn và hiển thị bảng kết quả latency từng bài test.

### Kiểm thử với thiết bị đang chạy độc lập
```bash
python3 scripts/test_protocol.py --host 127.0.0.1 --port 8001 --password 123456
```

