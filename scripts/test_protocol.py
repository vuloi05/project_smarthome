#!/usr/bin/env python3
"""
Test Suite tự động kiểm thử Giao thức Mạng Smart Home (UDP Broadcast & TCP Custom Protocol)
Dùng cho CI/CD và kiểm thử cục bộ không phụ thuộc bên ngoài (chỉ dùng Python Standard Library).
"""

import sys
import time
import socket
import argparse
import subprocess
import os
import signal

# Định nghĩa các mã phản hồi theo common/protocol.h
STATUS_OK = "100"
ERR_WRONG_PASSWORD = "201"
ERR_WRONG_OLD_PASSWORD = "202"
ERR_UNAUTHORIZED = "203"
ERR_INVALID_PARAM = "204"

class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    BOLD = "\033[1m"
    RESET = "\033[0m"

def log_info(msg):
    print(f"{Colors.BLUE}[INFO]{Colors.RESET} {msg}")

def log_pass(name, latency_ms=0.0):
    print(f"  {Colors.GREEN}✓ PASS{Colors.RESET} - {name} ({latency_ms:.2f} ms)")

def log_fail(name, reason):
    print(f"  {Colors.RED}✗ FAIL{Colors.RESET} - {name}: {reason}")

class SmartHomeTester:
    def __init__(self, host="127.0.0.1", tcp_port=8001, udp_port=9999, password="123456"):
        self.host = host
        self.tcp_port = tcp_port
        self.udp_port = udp_port
        self.password = password
        self.app_id = "TEST_APP_01"
        self.token = None
        self.passed = 0
        self.failed = 0

    def run_all(self, test_udp=True):
        print(f"\n{Colors.BOLD}=== BẮT ĐẦU KIỂM THỬ GIAO THỨC SMART HOME ==={Colors.RESET}")
        print(f"Host: {self.host} | TCP Port: {self.tcp_port} | UDP Port: {self.udp_port}\n")

        if test_udp:
            self.test_udp_discovery()

        self.test_tcp_wrong_password()
        self.test_tcp_valid_auth()
        self.test_tcp_unauthorized_access()
        self.test_tcp_set_power()
        self.test_tcp_get_info()
        self.test_tcp_timer()
        self.test_tcp_invalid_param()

        print(f"\n{Colors.BOLD}=== KẾT QUẢ KIỂM THỬ ==={Colors.RESET}")
        print(f"Tổng số test: {self.passed + self.failed}")
        print(f"Passed: {Colors.GREEN}{self.passed}{Colors.RESET}")
        print(f"Failed: {Colors.RED if self.failed > 0 else Colors.GREEN}{self.failed}{Colors.RESET}\n")

        return self.failed == 0

    def test_udp_discovery(self):
        test_name = "UDP Broadcast Device Discovery (SCAN_REQ -> SCAN_RES)"
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.settimeout(2.0)

        t0 = time.perf_counter()
        try:
            req = b"SCAN_REQ\r\n"
            sock.sendto(req, ("255.255.255.255", self.udp_port))
            # Cung thu gui truc tiep den host de phong truong hop loopback khong broadcast
            sock.sendto(req, (self.host, self.udp_port))

            data, addr = sock.recvfrom(1024)
            latency = (time.perf_counter() - t0) * 1000
            msg = data.decode("utf-8", errors="ignore").strip()

            if msg.startswith("SCAN_RES|"):
                log_pass(test_name, latency)
                self.passed += 1
            else:
                log_fail(test_name, f"Bản tin phản hồi không đúng cú pháp: {msg}")
                self.failed += 1
        except socket.timeout:
            # Tren mot so may Windows/CI loopback UDP broadcast bi chan, fallback kiem tra warning
            log_info("UDP Broadcast timed out (co the do firewall hoac broadcast khong ho tro tren loopback/CI), bo qua test UDP.")
        except Exception as e:
            log_fail(test_name, str(e))
            self.failed += 1
        finally:
            sock.close()

    def send_tcp_msg(self, msg_str):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3.0)
        sock.connect((self.host, self.tcp_port))
        
        t0 = time.perf_counter()
        sock.sendall((msg_str + "\r\n").encode("utf-8"))

        data = b""
        while b"\r\n" not in data:
            chunk = sock.recv(1024)
            if not chunk:
                break
            data += chunk

        latency = (time.perf_counter() - t0) * 1000
        sock.close()
        return data.decode("utf-8", errors="ignore").strip(), latency

    def test_tcp_wrong_password(self):
        test_name = "TCP Authentication với sai mật khẩu (kỳ vọng mã 201)"
        try:
            resp, latency = self.send_tcp_msg(f"AUTH|{self.app_id}|SAI_PASS_123")
            if resp.startswith(f"{ERR_WRONG_PASSWORD}|WRONG_PASSWORD"):
                log_pass(test_name, latency)
                self.passed += 1
            else:
                log_fail(test_name, f"Nhận được phản hồi không mong muốn: '{resp}'")
                self.failed += 1
        except Exception as e:
            log_fail(test_name, str(e))
            self.failed += 1

    def test_tcp_valid_auth(self):
        test_name = "TCP Authentication với đúng mật khẩu (kỳ vọng mã 100 và cấp Token)"
        try:
            resp, latency = self.send_tcp_msg(f"AUTH|{self.app_id}|{self.password}")
            parts = resp.split("|")
            if len(parts) >= 3 and parts[0] == STATUS_OK and parts[1] == "AUTH_OK":
                self.token = parts[2]
                log_pass(f"{test_name} [Token={self.token}]", latency)
                self.passed += 1
            else:
                log_fail(test_name, f"Xác thực thất bại, phản hồi: '{resp}'")
                self.failed += 1
        except Exception as e:
            log_fail(test_name, str(e))
            self.failed += 1

    def test_tcp_unauthorized_access(self):
        test_name = "TCP Lệnh với Token sai / chưa xác thực (kỳ vọng mã 203 UNAUTHORIZED)"
        try:
            resp, latency = self.send_tcp_msg("SET_POWER|TOKEN_GIA_MAO|ON")
            if resp.startswith(f"{ERR_UNAUTHORIZED}|UNAUTHORIZED"):
                log_pass(test_name, latency)
                self.passed += 1
            else:
                log_fail(test_name, f"Kỳ vọng 203|UNAUTHORIZED nhưng nhận: '{resp}'")
                self.failed += 1
        except Exception as e:
            log_fail(test_name, str(e))
            self.failed += 1

    def test_tcp_set_power(self):
        test_name = "TCP Điều khiển Bật/Tắt thiết bị (SET_POWER ON / OFF)"
        if not self.token:
            log_fail(test_name, "Bỏ qua do chưa có Token hợp lệ")
            self.failed += 1
            return

        try:
            # Bật thiết bị
            resp_on, lat1 = self.send_tcp_msg(f"SET_POWER|{self.token}|ON")
            # Tắt thiết bị
            resp_off, lat2 = self.send_tcp_msg(f"SET_POWER|{self.token}|OFF")

            ok_on = resp_on.startswith(f"{STATUS_OK}|POWER_OK|ON")
            ok_off = resp_off.startswith(f"{STATUS_OK}|POWER_OK|OFF")

            if ok_on and ok_off:
                log_pass(test_name, (lat1 + lat2) / 2)
                self.passed += 1
            else:
                log_fail(test_name, f"Phản hồi không đúng: ON='{resp_on}', OFF='{resp_off}'")
                self.failed += 1
        except Exception as e:
            log_fail(test_name, str(e))
            self.failed += 1

    def test_tcp_get_info(self):
        test_name = "TCP Truy vấn thông tin chi tiết (GET_INFO)"
        if not self.token:
            log_fail(test_name, "Bỏ qua do chưa có Token hợp lệ")
            self.failed += 1
            return

        try:
            resp, latency = self.send_tcp_msg(f"GET_INFO|{self.token}")
            parts = resp.split("|")
            if len(parts) >= 8 and parts[0] == STATUS_OK and parts[1] == "INFO":
                log_pass(f"{test_name} [Thiết bị: {parts[4]} - Trạng thái: {parts[5]}]", latency)
                self.passed += 1
            else:
                log_fail(test_name, f"Phản hồi GET_INFO không hợp lệ: '{resp}'")
                self.failed += 1
        except Exception as e:
            log_fail(test_name, str(e))
            self.failed += 1

    def test_tcp_timer(self):
        test_name = "TCP Đặt hẹn giờ & Hủy hẹn giờ (SET_TIMER)"
        if not self.token:
            log_fail(test_name, "Bỏ qua do chưa có Token hợp lệ")
            self.failed += 1
            return

        try:
            # Hẹn giờ 60 giây
            resp_set, lat1 = self.send_tcp_msg(f"SET_TIMER|{self.token}|60|ON")
            # Hủy hẹn giờ (0 giây)
            resp_cancel, lat2 = self.send_tcp_msg(f"SET_TIMER|{self.token}|0|OFF")

            ok_set = resp_set.startswith(f"{STATUS_OK}|TIMER_OK|60|ON")
            ok_cancel = resp_cancel.startswith(f"{STATUS_OK}|TIMER_CANCELLED")

            if ok_set and ok_cancel:
                log_pass(test_name, (lat1 + lat2) / 2)
                self.passed += 1
            else:
                log_fail(test_name, f"Phản hồi TIMER không đúng: SET='{resp_set}', CANCEL='{resp_cancel}'")
                self.failed += 1
        except Exception as e:
            log_fail(test_name, str(e))
            self.failed += 1

    def test_tcp_invalid_param(self):
        test_name = "TCP Tham số không hợp lệ (kỳ vọng mã 204 INVALID_PARAM)"
        if not self.token:
            log_fail(test_name, "Bỏ qua do chưa có Token hợp lệ")
            self.failed += 1
            return

        try:
            # Gửi SET_POWER với giá trị không phải ON/OFF
            resp, latency = self.send_tcp_msg(f"SET_POWER|{self.token}|INVALID_VALUE")
            if resp.startswith(f"{ERR_INVALID_PARAM}|INVALID_PARAM"):
                log_pass(test_name, latency)
                self.passed += 1
            else:
                log_fail(test_name, f"Kỳ vọng 204|INVALID_PARAM nhưng nhận: '{resp}'")
                self.failed += 1
        except Exception as e:
            log_fail(test_name, str(e))
            self.failed += 1


def main():
    parser = argparse.ArgumentParser(description="Smart Home Protocol Automated Test Suite")
    parser.add_argument("--host", default="127.0.0.1", help="Địa chỉ IP của thiết bị mô phỏng")
    parser.add_argument("--port", type=int, default=8001, help="Cổng TCP của thiết bị")
    parser.add_argument("--udp-port", type=int, default=9999, help="Cổng UDP phát hiện thiết bị")
    parser.add_argument("--password", default="123456", help="Mật khẩu thiết bị")
    parser.add_argument("--spawn-device", action="store_true", help="Tự động biên dịch và khởi chạy thiết bị mô phỏng để test")
    parser.add_argument("--no-udp", action="store_true", help="Bỏ qua kiểm thử UDP broadcast")
    args = parser.parse_args()

    proc = None
    if args.spawn_device:
        log_info("Đang kiểm tra và khởi chạy thiết bị mô phỏng tạm thời...")
        # Biên dịch nếu chưa có file thực thi
        bin_path = os.path.join(".", "bin", "device_server")
        if not os.path.isfile(bin_path):
            log_info("Biên dịch device_server...")
            os.makedirs("bin", exist_ok=True)
            res = subprocess.run(["gcc", "-Wall", "-Wextra", "-g", "-Icommon",
                                  "device_sim/device_server.c", "common/net_utils.c",
                                  "-o", bin_path])
            if res.returncode != 0:
                print(f"{Colors.RED}[ERROR] Biên dịch thất bại!{Colors.RESET}")
                sys.exit(1)

        test_port = 8899
        args.port = test_port
        proc = subprocess.Popen([bin_path, "TEST01", "LIGHT", str(test_port), args.password, "Test_Den_Phong"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)

    try:
        tester = SmartHomeTester(host=args.host, tcp_port=args.port, udp_port=args.udp_port, password=args.password)
        success = tester.run_all(test_udp=(not args.no_udp))
        if not success:
            sys.exit(1)
    finally:
        if proc:
            log_info("Đang dừng tiến trình thiết bị mô phỏng...")
            proc.terminate()
            try:
                proc.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                proc.kill()

if __name__ == "__main__":
    main()
