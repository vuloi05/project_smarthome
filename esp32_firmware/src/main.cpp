/**
 * ============================================================================
 * FIRMWARE THIẾT BỊ ĐIỆN THÔNG MINH TRÊN VI ĐIỀU KHIỂN ESP32 (IT4062)
 * ============================================================================
 * Bo mạch  : DOIT ESP32 DEVKIT V1 / ESP32 NodeMCU-32S (Chip ESP-WROOM-32)
 * Giao tiếp: Silicon Labs CP2102 USB to UART Bridge (VID: 10C4, PID: EA60)
 *
 * Phần cứng tích hợp sử dụng ngay trên bo mạch (không bắt buộc nối dây ngoài):
 *  - GPIO 2 : Đèn LED xanh tích hợp trên mạch (On-board Blue LED)
 *             + Chế độ LIGHT: Bật = Sáng 100%, Tắt = Tắt
 *             + Chế độ FAN  : Băm xung PWM độ sáng theo tốc độ gió (Mức 1: 30%, Mức 2: 65%, Mức 3: 100%)
 *             + Chế độ AC   : Băm xung PWM theo công suất làm lạnh/sưởi ấm
 *  - GPIO 0 : Nút nhấn vật lý "BOOT" trên mạch -> Bấm để Bật/Tắt thiết bị trực tiếp!
 *  - GPIO 4 : Chân mở rộng (nếu cắm thêm Module Quạt mini / Relay ngoài)
 *
 * Hỗ trợ cấu hình nhanh qua Serial Monitor (115200 baud) mà không cần nạp lại code:
 *  - Gõ: WIFI|<ten_wifi>|<mat_khau_wifi>   (Lưu vào bộ nhớ Flash NVS của ESP32)
 *  - Gõ: TYPE|<LIGHT|FAN|AC>|<id>|<ten>    (Đổi vai trò thiết bị ngay lập tức)
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>

/* -------------------- CẤU HÌNH MẶC ĐỊNH -------------------- */
#define DEFAULT_WIFI_SSID     "TEN_WIFI_CUA_BAN"
#define DEFAULT_WIFI_PASS     "MAT_KHAU_WIFI"

#define DEFAULT_DEV_ID        "ESP32_DEV01"
#define DEFAULT_DEV_TYPE      "LIGHT"         /* "LIGHT", "FAN", hoặc "AC" */
#define DEFAULT_DEV_NAME      "Den_ESP32_Thuc_Te"
#define DEFAULT_DEV_PASS      "123456"

#define DISCOVERY_UDP_PORT    9999
#define CONTROL_TCP_PORT      8001

/* Chân phần cứng trên DOIT ESP32 DEVKIT V1 */
#define PIN_ONBOARD_LED       2               /* GPIO 2: LED xanh tích hợp */
#define PIN_BOOT_BUTTON       0               /* GPIO 0: Nút nhấn BOOT trên mạch */
#define PIN_EXTERNAL_OUT      4               /* GPIO 4: Chân mở rộng cho Relay/Fan ngoài */

#define MAX_SESSIONS          16
#define MAX_TCP_CLIENTS       4

/* -------------------- CẤU TRÚC DỮ LIỆU -------------------- */
struct AuthSession {
    bool   active;
    String app_id;
    String token;
};

/* Biến trạng thái thiết bị */
Preferences prefs;
WiFiUDP     udp;
WiFiServer  tcpServer(CONTROL_TCP_PORT);
WiFiClient  tcpClients[MAX_TCP_CLIENTS];

String g_wifi_ssid;
String g_wifi_pass;

String g_dev_id;
String g_dev_type;     /* "LIGHT", "FAN", "AC" */
String g_dev_name;
String g_dev_pass;

bool   g_power_on       = false;
int    g_fan_speed      = 1;        /* 1, 2, 3 */
String g_ac_mode        = "COOL";   /* "COOL", "HEAT", "DRY" */
int    g_ac_temp        = 26;       /* 18 .. 30 */
double g_current_watt   = 0.0;

bool   g_timer_active   = false;
int    g_timer_rem      = 0;
bool   g_timer_target   = false;

unsigned long g_day_seconds   = 0;
unsigned long g_month_seconds = 0;
unsigned long g_year_seconds  = 0;
double        g_total_wh      = 0.0;

unsigned long g_last_tick_ms  = 0;
bool          g_last_btn_state = HIGH;
unsigned long g_last_btn_ms   = 0;

AuthSession g_sessions[MAX_SESSIONS];

/* -------------------- HÀM TIỆN ÍCH & PHẦN CỨNG -------------------- */

void logActivity(const String &msg) {
    unsigned long sec = millis() / 1000;
    Serial.printf("[%06lu s] [%s] %s\n", sec, g_dev_id.c_str(), msg.c_str());
}

double calculateWatt() {
    if (!g_power_on) return 0.0;

    if (g_dev_type == "LIGHT") {
        return 40.0;
    } else if (g_dev_type == "FAN") {
        if (g_fan_speed == 1) return 30.0;
        if (g_fan_speed == 2) return 50.0;
        if (g_fan_speed == 3) return 75.0;
        return 30.0;
    } else if (g_dev_type == "AC") {
        if (g_ac_mode == "DRY") return 600.0;
        if (g_ac_mode == "COOL") {
            int diff = 30 - g_ac_temp;
            if (diff < 0) diff = 0;
            return 1000.0 + diff * 50.0;
        }
        if (g_ac_mode == "HEAT") {
            int diff = g_ac_temp - 18;
            if (diff < 0) diff = 0;
            return 1000.0 + diff * 50.0;
        }
        return 1000.0;
    }
    return 40.0;
}

/* Cập nhật tín hiệu vật lý ra đèn LED GPIO 2 và chân mở rộng GPIO 4 */
void updateHardwareOutput() {
    g_current_watt = calculateWatt();

    if (!g_power_on) {
        analogWrite(PIN_ONBOARD_LED, 0);
        analogWrite(PIN_EXTERNAL_OUT, 0);
        return;
    }

    int pwm_duty = 255;
    if (g_dev_type == "LIGHT") {
        pwm_duty = 255; /* Sáng tối đa 100% */
    } else if (g_dev_type == "FAN") {
        if (g_fan_speed == 1) pwm_duty = 75;       /* ~30% */
        else if (g_fan_speed == 2) pwm_duty = 165; /* ~65% */
        else pwm_duty = 255;                       /* 100% */
    } else if (g_dev_type == "AC") {
        /* Độ sáng tỷ lệ với công suất điều hòa (600W -> 1600W) */
        pwm_duty = map((long)g_current_watt, 600, 1600, 80, 255);
        pwm_duty = constrain(pwm_duty, 60, 255);
    }

    analogWrite(PIN_ONBOARD_LED, pwm_duty);
    analogWrite(PIN_EXTERNAL_OUT, pwm_duty);
}

String generateToken() {
    const char hex[] = "0123456789ABCDEF";
    String tok = "";
    for (int i = 0; i < 16; i++) {
        tok += hex[esp_random() % 16];
    }
    return tok;
}

String createSessionToken(const String &app_id) {
    int free_idx = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].active && g_sessions[i].app_id == app_id) {
            g_sessions[i].token = generateToken();
            return g_sessions[i].token;
        }
        if (!g_sessions[i].active && free_idx == -1) {
            free_idx = i;
        }
    }
    if (free_idx == -1) free_idx = 0;
    g_sessions[free_idx].active = true;
    g_sessions[free_idx].app_id = app_id;
    g_sessions[free_idx].token  = generateToken();
    return g_sessions[free_idx].token;
}

String verifyToken(const String &token) {
    if (token.length() == 0) return "";
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (g_sessions[i].active && g_sessions[i].token == token) {
            return g_sessions[i].app_id;
        }
    }
    return "";
}

/* -------------------- XỬ LÝ GIAO THỨC TCP -------------------- */

String splitField(const String &data, char separator, int index) {
    int found = 0;
    int strIndex[] = {0, -1};
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex && data.charAt(i) != separator) ? i + 1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void handleClientCommand(WiFiClient &client, String line) {
    line.trim();
    if (line.length() == 0) return;

    String cmd = splitField(line, '|', 0);
    char resp[512];

    /* 1. Xác thực: AUTH|<app_id>|<password> */
    if (cmd == "AUTH") {
        String app_id = splitField(line, '|', 1);
        String pass   = splitField(line, '|', 2);

        if (app_id.length() == 0 || pass.length() == 0) {
            client.print("204|INVALID_PARAM\r\n");
            return;
        }

        if (pass == g_dev_pass) {
            String tok = createSessionToken(app_id);
            snprintf(resp, sizeof(resp), "100|AUTH_OK|%s|%s|%s|%s|%s|%.1f\r\n",
                     tok.c_str(), g_dev_id.c_str(), g_dev_type.c_str(),
                     g_dev_name.c_str(), g_power_on ? "ON" : "OFF", g_current_watt);
            client.print(resp);
            logActivity("[AUTH] AppID=" + app_id + " xac thuc thanh cong -> Token=" + tok);
        } else {
            client.print("201|WRONG_PASSWORD\r\n");
            logActivity("[AUTH] AppID=" + app_id + " nhap sai mat khau!");
        }
        return;
    }

    /* Kiểm tra Token cho tất cả các lệnh điều khiển */
    String token  = splitField(line, '|', 1);
    String app_id = verifyToken(token);
    if (app_id.length() == 0) {
        client.print("203|UNAUTHORIZED\r\n");
        logActivity("[SECURITY] Tu choi lenh '" + cmd + "' do Token khong hop le!");
        return;
    }

    /* 2. Đổi mật khẩu: PASSWD|<token>|<old_pass>|<new_pass> */
    if (cmd == "PASSWD") {
        String old_p = splitField(line, '|', 2);
        String new_p = splitField(line, '|', 3);

        if (old_p != g_dev_pass) {
            client.print("202|WRONG_OLD_PASSWORD\r\n");
            logActivity("[PASSWD] AppID=" + app_id + " doi mat khau that bai");
        } else if (new_p.length() == 0) {
            client.print("204|INVALID_PARAM\r\n");
        } else {
            g_dev_pass = new_p;
            prefs.putString("dev_pass", g_dev_pass);
            client.print("100|PASSWD_OK\r\n");
            logActivity("[PASSWD] AppID=" + app_id + " da doi mat khau thanh cong");
        }
    }
    /* 3. Bật / Tắt thiết bị: SET_POWER|<token>|<ON|OFF> */
    else if (cmd == "SET_POWER") {
        String st = splitField(line, '|', 2);
        st.toUpperCase();
        if (st != "ON" && st != "OFF") {
            client.print("204|INVALID_PARAM\r\n");
        } else {
            g_power_on = (st == "ON");
            updateHardwareOutput();
            snprintf(resp, sizeof(resp), "100|POWER_OK|%s|%.1f\r\n",
                     g_power_on ? "ON" : "OFF", g_current_watt);
            client.print(resp);
            logActivity("[CONTROL] AppID=" + app_id + " chuyen trang thai LED ESP32: " +
                        (g_power_on ? "ON" : "OFF") + " (" + String(g_current_watt, 1) + "W)");
        }
    }
    /* 4. Hẹn giờ: SET_TIMER|<token>|<seconds>|<ON|OFF> */
    else if (cmd == "SET_TIMER") {
        int secs  = splitField(line, '|', 2).toInt();
        String st = splitField(line, '|', 3);
        st.toUpperCase();

        if (secs == 0) {
            g_timer_active = false;
            g_timer_rem    = 0;
            client.print("100|TIMER_CANCELLED\r\n");
            logActivity("[TIMER] AppID=" + app_id + " huy hen gio");
        } else if (secs > 0 && (st == "ON" || st == "OFF")) {
            g_timer_active = true;
            g_timer_rem    = secs;
            g_timer_target = (st == "ON");
            snprintf(resp, sizeof(resp), "100|TIMER_OK|%d|%s\r\n",
                     secs, g_timer_target ? "ON" : "OFF");
            client.print(resp);
            logActivity("[TIMER] AppID=" + app_id + " hen gio sau " + String(secs) + "s se " + st);
        } else {
            client.print("204|INVALID_PARAM\r\n");
        }
    }
    /* 5. Chỉnh tốc độ gió Quạt: SET_FAN|<token>|<1|2|3> */
    else if (cmd == "SET_FAN") {
        int spd = splitField(line, '|', 2).toInt();
        if (g_dev_type != "FAN" || spd < 1 || spd > 3) {
            client.print("204|INVALID_PARAM\r\n");
        } else {
            g_fan_speed = spd;
            updateHardwareOutput();
            snprintf(resp, sizeof(resp), "100|FAN_OK|%d|%.1f\r\n", g_fan_speed, g_current_watt);
            client.print(resp);
            logActivity("[CONTROL] AppID=" + app_id + " chinh toc do Quat muc " + String(g_fan_speed));
        }
    }
    /* 6. Chỉnh chế độ Điều hòa: SET_AC|<token>|<COOL|HEAT|DRY>|<18..30> */
    else if (cmd == "SET_AC") {
        String mode = splitField(line, '|', 2);
        mode.toUpperCase();
        int temp = splitField(line, '|', 3).toInt();

        if (g_dev_type != "AC" || temp < 18 || temp > 30 ||
            (mode != "COOL" && mode != "HEAT" && mode != "DRY")) {
            client.print("204|INVALID_PARAM\r\n");
        } else {
            g_ac_mode = mode;
            g_ac_temp = temp;
            updateHardwareOutput();
            snprintf(resp, sizeof(resp), "100|AC_OK|%s|%d|%.1f\r\n",
                     g_ac_mode.c_str(), g_ac_temp, g_current_watt);
            client.print(resp);
            logActivity("[CONTROL] AppID=" + app_id + " chinh AC: " + g_ac_mode + " " + String(g_ac_temp) + "C");
        }
    }
    /* 7. Lấy thông tin chi tiết & Thống kê điện năng: GET_INFO|<token> */
    else if (cmd == "GET_INFO") {
        snprintf(resp, sizeof(resp),
                 "100|INFO|%s|%s|%s|%s|%.1f|%d|%s|%d|%d|%s|%lu|%lu|%lu|%.4f\r\n",
                 g_dev_id.c_str(),
                 g_dev_type.c_str(),
                 g_dev_name.c_str(),
                 g_power_on ? "ON" : "OFF",
                 g_current_watt,
                 g_fan_speed,
                 g_ac_mode.c_str(),
                 g_ac_temp,
                 g_timer_active ? g_timer_rem : 0,
                 g_timer_active ? (g_timer_target ? "ON" : "OFF") : "NONE",
                 g_day_seconds,
                 g_month_seconds,
                 g_year_seconds,
                 g_total_wh);
        client.print(resp);
        logActivity("[INFO] AppID=" + app_id + " xem thong tin chi tiet");
    } else {
        client.print("205|UNKNOWN_CMD\r\n");
    }
}

/* -------------------- CẤU HÌNH QUA SERIAL MONITOR -------------------- */

void checkSerialConfig() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    String tag = splitField(cmd, '|', 0);
    tag.toUpperCase();

    if (tag == "WIFI") {
        String ssid = splitField(cmd, '|', 1);
        String pass = splitField(cmd, '|', 2);
        if (ssid.length() > 0) {
            prefs.putString("wifi_ssid", ssid);
            prefs.putString("wifi_pass", pass);
            Serial.printf("\n[CONFIG] Da luu Wi-Fi moi: SSID='%s'. Dang ket noi lai...\n", ssid.c_str());
            ESP.restart();
        }
    } else if (tag == "TYPE") {
        String t    = splitField(cmd, '|', 1);
        String id   = splitField(cmd, '|', 2);
        String name = splitField(cmd, '|', 3);
        t.toUpperCase();
        if (t == "LIGHT" || t == "FAN" || t == "AC") {
            prefs.putString("dev_type", t);
            if (id.length() > 0)   prefs.putString("dev_id", id);
            if (name.length() > 0) prefs.putString("dev_name", name);
            Serial.println("\n[CONFIG] Da cap nhat loai thiet bi. Dang khoi dong lai...");
            ESP.restart();
        }
    } else if (tag == "STATUS") {
        Serial.println("\n--- TRANG THAI ESP32 HIEN TAI ---");
        Serial.printf("  ID: %s | Loai: %s | Ten: %s\n", g_dev_id.c_str(), g_dev_type.c_str(), g_dev_name.c_str());
        Serial.printf("  Wi-Fi IP: %s | TCP Port: %d | UDP Port: %d\n",
                      WiFi.localIP().toString().c_str(), CONTROL_TCP_PORT, DISCOVERY_UDP_PORT);
        Serial.printf("  Power: %s | Watt: %.1fW | Dien nang: %.4f Wh\n",
                      g_power_on ? "ON" : "OFF", g_current_watt, g_total_wh);
    }
}

/* -------------------- SETUP & LOOP -------------------- */

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(PIN_ONBOARD_LED, OUTPUT);
    pinMode(PIN_EXTERNAL_OUT, OUTPUT);
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
    analogWrite(PIN_ONBOARD_LED, 0);

    prefs.begin("smarthome", false);
    g_wifi_ssid = prefs.getString("wifi_ssid", DEFAULT_WIFI_SSID);
    g_wifi_pass = prefs.getString("wifi_pass", DEFAULT_WIFI_PASS);
    g_dev_id    = prefs.getString("dev_id",    DEFAULT_DEV_ID);
    g_dev_type  = prefs.getString("dev_type",  DEFAULT_DEV_TYPE);
    g_dev_name  = prefs.getString("dev_name",  DEFAULT_DEV_NAME);
    g_dev_pass  = prefs.getString("dev_pass",  DEFAULT_DEV_PASS);

    Serial.println("\n================================================================");
    Serial.println("   ESP32 SMART HOME DEVICE SERVER (DOIT ESP32 DEVKIT V1)       ");
    Serial.println("================================================================");
    Serial.printf("  Thiet bi : ID=%s | Loai=%s | Ten=%s\n",
                  g_dev_id.c_str(), g_dev_type.c_str(), g_dev_name.c_str());
    Serial.println("  Meo cau hinh nhanh qua Serial Monitor (115200 baud):");
    Serial.println("   -> Doi Wi-Fi    : WIFI|<ten_wifi>|<mat_khau>");
    Serial.println("   -> Doi Thiet bi : TYPE|<LIGHT|FAN|AC>|<ma_id>|<ten_thiet_bi>");
    Serial.println("   -> Xem IP/TT    : STATUS");
    Serial.println("================================================================");

    WiFi.mode(WIFI_STA);
    WiFi.begin(g_wifi_ssid.c_str(), g_wifi_pass.c_str());
    Serial.printf("Dang ket noi Wi-Fi '%s' ", g_wifi_ssid.c_str());

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 30) {
        delay(500);
        Serial.print(".");
        checkSerialConfig();
        retry++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[OK] Da ket noi Wi-Fi thanh cong!");
        Serial.print("  -> Dia chi IP cua ESP32 : ");
        Serial.println(WiFi.localIP());
        Serial.printf("  -> UDP Discovery Port   : %d\n", DISCOVERY_UDP_PORT);
        Serial.printf("  -> TCP Control Port     : %d\n", CONTROL_TCP_PORT);

        /* Nháy đèn LED 3 lần báo hiệu kết nối mạng thành công */
        for (int i = 0; i < 3; i++) {
            analogWrite(PIN_ONBOARD_LED, 255);
            delay(150);
            analogWrite(PIN_ONBOARD_LED, 0);
            delay(150);
        }
    } else {
        Serial.println("[!] Chua ket noi duoc Wi-Fi. Hay nhap lenh WIFI|<ssid>|<pass> vao Serial Monitor!");
    }

    udp.begin(DISCOVERY_UDP_PORT);
    tcpServer.begin();
    g_last_tick_ms = millis();
}

void loop() {
    /* 1. Kiểm tra lệnh cấu hình từ Serial Monitor */
    checkSerialConfig();

    /* 2. Kiểm tra nút nhấn vật lý BOOT (GPIO 0) trên mạch ESP32 */
    bool btn_state = digitalRead(PIN_BOOT_BUTTON);
    if (btn_state == LOW && g_last_btn_state == HIGH && (millis() - g_last_btn_ms > 250)) {
        g_last_btn_ms = millis();
        g_power_on = !g_power_on;
        updateHardwareOutput();
        logActivity(String("[BUTTON] Nhan nut BOOT tren mach ESP32 -> Chuyen trang thai sang ") +
                    (g_power_on ? "ON" : "OFF"));
    }
    g_last_btn_state = btn_state;

    /* 3. Bộ đếm thời gian mỗi 1 giây (1000ms): Tính điện năng & Hẹn giờ */
    if (millis() - g_last_tick_ms >= 1000) {
        unsigned long elapsed_sec = (millis() - g_last_tick_ms) / 1000;
        g_last_tick_ms += elapsed_sec * 1000;

        if (g_power_on) {
            g_day_seconds   += elapsed_sec;
            g_month_seconds += elapsed_sec;
            g_year_seconds  += elapsed_sec;
            g_total_wh      += g_current_watt * ((double)elapsed_sec / 3600.0);
        }

        if (g_timer_active) {
            g_timer_rem -= (int)elapsed_sec;
            if (g_timer_rem <= 0) {
                g_timer_rem    = 0;
                g_timer_active = false;
                g_power_on     = g_timer_target;
                updateHardwareOutput();
                logActivity(String("[TIMER] Hen gio kich hoat -> Chuyen trang thai sang ") +
                            (g_power_on ? "ON" : "OFF"));
            }
        }
    }

    /* 4. Lắng nghe gói tin Quét thiết bị qua UDP (Port 9999) */
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
        char buf[256];
        int len = udp.read(buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            String req = String(buf);
            req.trim();
            if (req == "SCAN_REQ") {
                char resp[256];
                snprintf(resp, sizeof(resp), "SCAN_RES|%s|%s|%d|%s\r\n",
                         g_dev_id.c_str(), g_dev_type.c_str(), CONTROL_TCP_PORT, g_dev_name.c_str());
                udp.beginPacket(udp.remoteIP(), udp.remotePort());
                udp.write((const uint8_t *)resp, strlen(resp));
                udp.endPacket();
                logActivity("[DISCOVERY] Phan hoi lenh quet tu " + udp.remoteIP().toString());
            }
        }
    }

    /* 5. Chấp nhận và xử lý các kết nối điều khiển qua TCP (Port 8001) */
    if (tcpServer.hasClient()) {
        bool accepted = false;
        for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
            if (!tcpClients[i] || !tcpClients[i].connected()) {
                if (tcpClients[i]) tcpClients[i].stop();
                tcpClients[i] = tcpServer.available();
                logActivity("[CONNECT] TCP Client ket noi tu " + tcpClients[i].remoteIP().toString());
                accepted = true;
                break;
            }
        }
        if (!accepted) {
            WiFiClient busyClient = tcpServer.available();
            busyClient.stop();
        }
    }

    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (tcpClients[i] && tcpClients[i].connected() && tcpClients[i].available()) {
            String line = tcpClients[i].readStringUntil('\n');
            handleClientCommand(tcpClients[i], line);
        }
    }
}
