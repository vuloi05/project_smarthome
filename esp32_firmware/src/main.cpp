/**
 * ============================================================================
 * FIRMWARE THIẾT BỊ ĐIỆN THÔNG MINH TRÊN VI ĐIỀU KHIỂN ESP32 (IT4062)
 * ============================================================================
 * Bo mạch  : DOIT ESP32 DEVKIT V1 / ESP32 NodeMCU-32S (Chip ESP-WROOM-32)
 * Giao tiếp: Silicon Labs CP2102 USB to UART Bridge (VID: 10C4, PID: EA60)
 *
 * TÍNH NĂNG MẠNG KÉP (WIFI_AP_STA):
 *  1. Chế độ phát Wi-Fi riêng (Access Point - AP):
 *     - Tên Wi-Fi (SSID) : SmartHome_ESP32
 *     - Mật khẩu Wi-Fi   : 12345678
 *     - IP cố định ESP32 : 192.168.4.1 (Broadcast: 192.168.4.255)
 *     -> Giúp Laptop và Điện thoại kết nối trực tiếp với ESP32 mọi lúc mọi nơi
 *        (kể cả trên phòng học không có Wi-Fi hoặc Wi-Fi trường chặn LAN).
 *  2. Chế độ thu Wi-Fi (Station - STA):
 *     - Kết nối đồng thời vào Router Wi-Fi gia đình / Hotspot điện thoại.
 *  3. Dịch vụ chạy đồng thời trên ESP32:
 *     - UDP Port 9999 : Lắng nghe lệnh quét SCAN_REQ và phản hồi SCAN_RES
 *     - TCP Port 8001 : Lắng nghe lệnh điều khiển theo giao thức đồ án (AUTH, SET_POWER...)
 *     - HTTP Port 80  : Giao diện Web trực quan tại http://192.168.4.1
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>

/* -------------------- CẤU HÌNH MẶC ĐỊNH -------------------- */
#define AP_SSID_PREFIX        "SmartHome_"
#define AP_DEFAULT_PASS       "12345678"

#define DEFAULT_WIFI_SSID     ""
#define DEFAULT_WIFI_PASS     ""

#define DEFAULT_DEV_ID        "ESP32_DEV01"
#define DEFAULT_DEV_TYPE      "LIGHT"         /* "LIGHT", "FAN", hoặc "AC" */
#define DEFAULT_DEV_NAME      "Thiet_Bi_ESP32"
#define DEFAULT_DEV_PASS      "123456"

#define DISCOVERY_UDP_PORT    9999
#define CONTROL_TCP_PORT      8001
#define WEB_HTTP_PORT         80

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

Preferences prefs;
WiFiUDP     udp;
WiFiServer  tcpServer(CONTROL_TCP_PORT);
WiFiClient  tcpClients[MAX_TCP_CLIENTS];
WebServer   webServer(WEB_HTTP_PORT);

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

unsigned long g_last_tick_ms   = 0;
bool          g_last_btn_state = HIGH;
unsigned long g_last_btn_ms    = 0;

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

void updateHardwareOutput() {
    g_current_watt = calculateWatt();

    if (!g_power_on) {
        analogWrite(PIN_ONBOARD_LED, 0);
        analogWrite(PIN_EXTERNAL_OUT, 0);
        return;
    }

    int pwm_duty = 255;
    if (g_dev_type == "LIGHT") {
        pwm_duty = 255;
    } else if (g_dev_type == "FAN") {
        if (g_fan_speed == 1) pwm_duty = 75;
        else if (g_fan_speed == 2) pwm_duty = 165;
        else pwm_duty = 255;
    } else if (g_dev_type == "AC") {
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

/* -------------------- GIAO DIỆN WEB TRỰC TIẾP TRÊN ESP32 (PORT 80) -------------------- */

void setupWebServer() {
    webServer.on("/", HTTP_GET, []() {
        String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                      "<title>ESP32 Smart Home Node</title>"
                      "<style>"
                      "body{font-family:system-ui,sans-serif;background:#0f172a;color:#f8fafc;margin:0;padding:20px;}"
                      ".card{max-width:520px;margin:0 auto;background:#1e293b;border-radius:14px;padding:22px;box-shadow:0 8px 24px rgba(0,0,0,0.4);}"
                      "h2{margin-top:0;color:#38bdf8;}"
                      ".badge{display:inline-block;padding:4px 12px;border-radius:99px;font-weight:700;font-size:13px;}"
                      ".on{background:#22c55e;color:#fff;} .off{background:#64748b;color:#fff;}"
                      ".row{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid #334155;font-size:14px;}"
                      ".btn{display:inline-block;padding:10px 16px;margin:4px 2px;border:none;border-radius:8px;font-weight:600;cursor:pointer;color:#fff;text-decoration:none;font-size:14px;}"
                      ".btn-on{background:#16a34a;} .btn-off{background:#dc2626;} .btn-act{background:#2563eb;}"
                      "input,select{padding:8px;border-radius:6px;border:1px solid #475569;background:#0f172a;color:#fff;margin:4px 0;}"
                      "</style></head><body><div class='card'>";

        html += "<h2>⚡ ESP32 Smart Device (" + g_dev_id + ")</h2>";
        html += "<div class='row'><span>Tên thiết bị:</span><b>" + g_dev_name + "</b></div>";
        html += "<div class='row'><span>Loại thiết bị:</span><b>" + g_dev_type + "</b></div>";
        html += "<div class='row'><span>Trạng thái:</span><span class='badge " + String(g_power_on ? "on'>ĐANG BẬT (ON)" : "off'>ĐANG TẮT (OFF)") + "</span></div>";
        html += "<div class='row'><span>Công suất tức thời:</span><b>" + String(g_current_watt, 1) + " W</b></div>";
        if (g_dev_type == "FAN") {
            html += "<div class='row'><span>Tốc độ gió Quạt:</span><b>Mức " + String(g_fan_speed) + "</b></div>";
        } else if (g_dev_type == "AC") {
            html += "<div class='row'><span>Chế độ Điều hòa:</span><b>" + g_ac_mode + " - " + String(g_ac_temp) + "°C</b></div>";
        }
        html += "<div class='row'><span>Thời gian chạy (Ngày/Tháng/Năm):</span><b>" +
                String(g_day_seconds) + "s / " + String(g_month_seconds) + "s / " + String(g_year_seconds) + "s</b></div>";
        html += "<div class='row'><span>Điện năng tiêu thụ:</span><b>" + String(g_total_wh, 4) + " Wh</b></div>";
        html += "<div class='row'><span>IP AP / IP Wi-Fi:</span><b>" + WiFi.softAPIP().toString() + " / " + WiFi.localIP().toString() + " (TCP:" + String(CONTROL_TCP_PORT) + ")</b></div>";

        html += "<div style='margin-top:16px;'><b>Điều khiển nhanh trên mạch:</b><br>";
        html += "<a class='btn btn-on' href='/ctrl?power=ON'>Bật (ON)</a> ";
        html += "<a class='btn btn-off' href='/ctrl?power=OFF'>Tắt (OFF)</a> ";
        if (g_dev_type == "FAN") {
            html += "<a class='btn btn-act' href='/ctrl?fan=1'>Gió 1</a> ";
            html += "<a class='btn btn-act' href='/ctrl?fan=2'>Gió 2</a> ";
            html += "<a class='btn btn-act' href='/ctrl?fan=3'>Gió 3</a>";
        }
        html += "</div>";

        html += "<hr style='border-color:#334155;margin:16px 0;'>";
        html += "<form action='/config' method='GET'><b>Đổi vai trò thiết bị ESP32:</b><br>";
        html += "Loại: <select name='type'><option value='LIGHT'>LIGHT (Bóng đèn)</option><option value='FAN'>FAN (Quạt)</option><option value='AC'>AC (Điều hòa)</option></select> ";
        html += "ID: <input name='id' value='" + g_dev_id + "' size='10'> ";
        html += "Tên: <input name='name' value='" + g_dev_name + "' size='14'> ";
        html += "<button class='btn btn-act' type='submit'>Lưu</button></form>";

        html += "</div></body></html>";
        webServer.send(200, "text/html", html);
    });

    webServer.on("/ctrl", HTTP_GET, []() {
        if (webServer.hasArg("power")) {
            g_power_on = (webServer.arg("power") == "ON");
            updateHardwareOutput();
            logActivity(String("[WEB] Chuyen trang thai qua Web UI: ") + (g_power_on ? "ON" : "OFF"));
        }
        if (webServer.hasArg("fan")) {
            int spd = webServer.arg("fan").toInt();
            if (spd >= 1 && spd <= 3) {
                g_fan_speed = spd;
                updateHardwareOutput();
                logActivity("[WEB] Chinh toc do Quat qua Web UI: Muc " + String(g_fan_speed));
            }
        }
        webServer.sendHeader("Location", "/");
        webServer.send(302, "text/plain", "");
    });

    webServer.on("/config", HTTP_GET, []() {
        if (webServer.hasArg("type")) {
            String t = webServer.arg("type");
            if (t == "LIGHT" || t == "FAN" || t == "AC") {
                g_dev_type = t;
                prefs.putString("dev_type", g_dev_type);
            }
        }
        if (webServer.hasArg("id") && webServer.arg("id").length() > 0) {
            g_dev_id = webServer.arg("id");
            prefs.putString("dev_id", g_dev_id);
        }
        if (webServer.hasArg("name") && webServer.arg("name").length() > 0) {
            g_dev_name = webServer.arg("name");
            prefs.putString("dev_name", g_dev_name);
        }
        updateHardwareOutput();
        webServer.sendHeader("Location", "/");
        webServer.send(302, "text/plain", "");
    });

    webServer.begin();
}

/* -------------------- XỬ LÝ GIAO THỨC SOCKET TCP (PORT 8001) -------------------- */

void handleClientCommand(WiFiClient &client, String line) {
    line.trim();
    if (line.length() == 0) return;

    String cmd = splitField(line, '|', 0);
    char resp[512];

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

    String token  = splitField(line, '|', 1);
    String app_id = verifyToken(token);
    if (app_id.length() == 0) {
        client.print("203|UNAUTHORIZED\r\n");
        logActivity("[SECURITY] Tu choi lenh '" + cmd + "' do Token khong hop le!");
        return;
    }

    if (cmd == "PASSWD") {
        String old_p = splitField(line, '|', 2);
        String new_p = splitField(line, '|', 3);

        if (old_p != g_dev_pass) {
            client.print("202|WRONG_OLD_PASSWORD\r\n");
        } else if (new_p.length() == 0) {
            client.print("204|INVALID_PARAM\r\n");
        } else {
            g_dev_pass = new_p;
            prefs.putString("dev_pass", g_dev_pass);
            client.print("100|PASSWD_OK\r\n");
            logActivity("[PASSWD] AppID=" + app_id + " da doi mat khau thanh cong");
        }
    }
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
            Serial.printf("\n[CONFIG] Da luu Wi-Fi moi: SSID='%s'. Dang khoi dong lai...\n", ssid.c_str());
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
        Serial.printf("  [AP Mode]  SSID: %s%s | IP: %s\n",
                      AP_SSID_PREFIX, g_dev_id.c_str(), WiFi.softAPIP().toString().c_str());
        Serial.printf("  [STA Mode] IP: %s | TCP Port: %d | UDP Port: %d\n",
                      WiFi.localIP().toString().c_str(), CONTROL_TCP_PORT, DISCOVERY_UDP_PORT);
        Serial.printf("  Power: %s | Watt: %.1fW | Dien nang: %.4f Wh\n",
                      g_power_on ? "ON" : "OFF", g_current_watt, g_total_wh);
    }
}

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

    /* Bật chế độ kép: Vừa tự phát Wi-Fi (AP) vừa kết nối Wi-Fi (STA) */
    WiFi.mode(WIFI_AP_STA);
    String ap_ssid = String(AP_SSID_PREFIX) + g_dev_id;
    WiFi.softAP(ap_ssid.c_str(), AP_DEFAULT_PASS);

    Serial.println("\n================================================================");
    Serial.println("   ESP32 SMART HOME DEVICE (WIFI AP + STA + WEB + SOCKET)      ");
    Serial.println("================================================================");
    Serial.printf("  Thiet bi        : ID=%s | Loai=%s | Ten=%s\n",
                  g_dev_id.c_str(), g_dev_type.c_str(), g_dev_name.c_str());
    Serial.printf("  [1] Wi-Fi Phat  : SSID='%s' | Pass='%s'\n", ap_ssid.c_str(), AP_DEFAULT_PASS);
    Serial.printf("      -> IP AP    : %s (Web UI: http://%s)\n",
                  WiFi.softAPIP().toString().c_str(), WiFi.softAPIP().toString().c_str());
    Serial.printf("      -> TCP Port : %d | UDP Discovery Port: %d\n",
                  CONTROL_TCP_PORT, DISCOVERY_UDP_PORT);

    if (g_wifi_ssid.length() > 0) {
        WiFi.begin(g_wifi_ssid.c_str(), g_wifi_pass.c_str());
        Serial.printf("  [2] Dang ket noi Wi-Fi STA '%s' ", g_wifi_ssid.c_str());
        int retry = 0;
        while (WiFi.status() != WL_CONNECTED && retry < 16) {
            delay(400);
            Serial.print(".");
            checkSerialConfig();
            retry++;
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("      -> Da ket noi STA! IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("      -> Khong ket noi duoc STA (Van hoat dong binh thuong qua Wi-Fi AP 192.168.4.1)");
        }
    } else {
        Serial.println("  [2] Chua cau hinh Wi-Fi STA (Dung Wi-Fi AP 192.168.4.1 hoac go WIFI|<ssid>|<pass>)");
    }
    Serial.println("================================================================");

    /* Nháy LED 3 lần báo hiệu mạch đã sẵn sàng */
    for (int i = 0; i < 3; i++) {
        analogWrite(PIN_ONBOARD_LED, 255);
        delay(120);
        analogWrite(PIN_ONBOARD_LED, 0);
        delay(120);
    }

    udp.begin(DISCOVERY_UDP_PORT);
    tcpServer.begin();
    setupWebServer();
    g_last_tick_ms = millis();
}

void loop() {
    checkSerialConfig();
    webServer.handleClient();

    /* Kiểm tra nút nhấn vật lý BOOT (GPIO 0) trên mạch ESP32 */
    bool btn_state = digitalRead(PIN_BOOT_BUTTON);
    if (btn_state == LOW && g_last_btn_state == HIGH && (millis() - g_last_btn_ms > 250)) {
        g_last_btn_ms = millis();
        g_power_on = !g_power_on;
        updateHardwareOutput();
        logActivity(String("[BUTTON] Nhan nut BOOT tren mach -> Chuyen trang thai sang ") +
                    (g_power_on ? "ON" : "OFF"));
    }
    g_last_btn_state = btn_state;

    /* Bộ đếm 1 giây: Tính điện năng & Hẹn giờ */
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

    /* Lắng nghe gói tin Quét thiết bị qua UDP (Port 9999) */
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

    /* Chấp nhận và xử lý kết nối điều khiển qua TCP (Port 8001) */
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
