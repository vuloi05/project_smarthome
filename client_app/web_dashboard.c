#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <poll.h>

#include "../common/protocol.h"
#include "../common/net_utils.h"
#include "home_manager.h"

#define DB_FILE          "client_data.db"
#define DEFAULT_WEB_PORT 8080

static UserAppData g_app;

/* ==================== HÀM TIỆN ÍCH URL & JSON ==================== */

/* Giải mã chuỗi URL-encoded (%20, +, ...) */
void url_decode(char *dst, const char *src, size_t dst_size) {
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_size; si++) {
        if (src[si] == '+') {
            dst[di++] = ' ';
        } else if (src[si] == '%' && isxdigit((unsigned char)src[si + 1]) && isxdigit((unsigned char)src[si + 2])) {
            char hex[3] = { src[si + 1], src[si + 2], '\0' };
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

/* Lấy giá trị tham số từ query string (vd: "id=L01&pass=123456") */
int get_query_param(const char *query, const char *key, char *out, size_t out_size) {
    out[0] = '\0';
    if (!query || !key) return 0;

    char buf[1024];
    strncpy(buf, query, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    size_t key_len = strlen(key);
    char *saveptr = NULL;
    char *pair = strtok_r(buf, "&", &saveptr);
    while (pair) {
        if (strncmp(pair, key, key_len) == 0 && pair[key_len] == '=') {
            url_decode(out, pair + key_len + 1, out_size);
            return 1;
        }
        pair = strtok_r(NULL, "&", &saveptr);
    }
    return 0;
}

/* Thoát ký tự đặc biệt cho chuỗi JSON */
void json_escape(const char *src, char *dst, size_t dst_size) {
    size_t di = 0;
    for (size_t si = 0; src && src[si] != '\0' && di + 2 < dst_size; si++) {
        if (src[si] == '"' || src[si] == '\\') {
            dst[di++] = '\\';
            dst[di++] = src[si];
        } else if ((unsigned char)src[si] >= 32) {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

/* ==================== GIAO TIẾP TCP/UDP VỚI THIẾT BỊ ==================== */

int send_device_tcp_cmd_fast(const char *ip, int port, const char *cmd_line, char *resp_buf, size_t resp_size, int timeout_ms) {
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return -1;

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        return -1;
    }

    if (send_line(sock, cmd_line) < 0) {
        close(sock);
        return -1;
    }

    int n = recv_line(sock, resp_buf, resp_size);
    close(sock);
    return (n > 0) ? 0 : -1;
}

/* ==================== GIAO DIỆN WEB HTML5/JS NHÚNG ==================== */

static const char *DASHBOARD_HTML =
"<!DOCTYPE html><html lang='vi'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Smart Home Central Dashboard - IT4062</title>"
"<style>"
"*{box-sizing:border-box;font-family:'Segoe UI',system-ui,sans-serif}"
"body{margin:0;background:#0b1120;color:#f8fafc;padding:20px}"
".header{max-width:1180px;margin:0 auto 20px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:12px;background:#1e293b;padding:16px 22px;border-radius:14px;border:1px solid #334155}"
".header h1{margin:0;font-size:1.35rem;color:#38bdf8}"
".sub{color:#94a3b8;font-size:.85rem;margin-top:4px}"
".grid{max-width:1180px;margin:0 auto;display:grid;grid-template-columns:340px 1fr;gap:20px}"
"@media(max-width:900px){.grid{grid-template-columns:1fr}}"
".panel{background:#1e293b;border-radius:14px;padding:18px;border:1px solid #334155;margin-bottom:18px}"
".panel h2{margin:0 0 12px;font-size:1.05rem;color:#e2e8f0;border-bottom:1px solid #334155;padding-bottom:8px}"
"input,select,button{padding:9px 12px;border-radius:8px;border:1px solid #475569;background:#0f172a;color:#f8fafc;font-size:.9rem}"
"button{cursor:pointer;background:#0284c7;border:none;font-weight:600;transition:.15s}"
"button:hover{background:#0369a1}"
".btn-sm{padding:6px 10px;font-size:.8rem}"
".btn-ok{background:#16a34a}.btn-ok:hover{background:#15803d}"
".btn-err{background:#dc2626}.btn-err:hover{background:#b91c1c}"
".btn-warn{background:#d97706}.btn-warn:hover{background:#b45309}"
".row{display:flex;gap:8px;margin-bottom:10px;flex-wrap:wrap}"
".row input,.row select{flex:1;min-width:90px}"
".dev-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:16px}"
".dev-card{background:#0f172a;border:1px solid #334155;border-radius:12px;padding:16px;position:relative}"
".dev-card.on{border-color:#22c55e;box-shadow:0 0 15px rgba(34,197,94,.12)}"
".badge{display:inline-block;padding:3px 9px;border-radius:99px;font-size:.75rem;font-weight:700}"
".b-on{background:#16a34a;color:#fff}.b-off{background:#475569;color:#e2e8f0}.b-err{background:#dc2626;color:#fff}"
".stat-line{display:flex;justify-content:space-between;font-size:.83rem;color:#94a3b8;padding:4px 0;border-bottom:1px dashed #1e293b}"
".stat-line b{color:#e2e8f0}"
".toast{position:fixed;bottom:20px;right:20px;background:#0284c7;color:#fff;padding:12px 18px;border-radius:10px;box-shadow:0 8px 25px rgba(0,0,0,.4);display:none;z-index:999}"
".scan-item{background:#0f172a;padding:10px;border-radius:8px;border:1px solid #334155;margin-top:8px;font-size:.84rem}"
"</style></head><body>"
"<div class='header'>"
"<div><h1>🏠 HỆ THỐNG ĐIỀU KHIỂN THIẾT BỊ ĐIỆN THÔNG MINH (WEB GATEWAY)</h1>"
"<div class='sub'>App ID: <b id='appId'>...</b> | Giao thức: UDP Broadcast (9999) & TCP Line Stream | Hỗ trợ Linux Sim & Mạch thật ESP32</div></div>"
"<div><button onclick='loadState()'>🔄 Làm mới trạng thái</button></div>"
"</div>"
"<div class='grid'>"
"<div>"
"  <div class='panel'><h2>📡 1. Quét Thiết Bị Trên Mạng (UDP 9999)</h2>"
"    <div class='row'><input id='scanIp' placeholder='IP ESP32 / Broadcast (tùy chọn, vd 192.168.4.1)'><button onclick='scanDevs()'>Quét Ngay</button></div>"
"    <div id='scanList' style='color:#94a3b8;font-size:.85rem'>Nhấn Quét Ngay để tìm Bóng đèn, Quạt, Điều hòa, ESP32...</div>"
"  </div>"
"  <div class='panel'><h2>🏘️ 2. Quản Lý Căn Nhà</h2>"
"    <div class='row'><input id='houseName' placeholder='Tên căn nhà mới...'><button class='btn-ok' onclick='addHouse()'>+ Thêm</button></div>"
"    <div id='houseList'></div>"
"  </div>"
"  <div class='panel'><h2>🚪 3. Quản Lý Phòng</h2>"
"    <div class='row'><select id='roomHouseSel'></select><input id='roomName' placeholder='Tên phòng...'><button class='btn-ok' onclick='addRoom()'>+ Thêm</button></div>"
"    <div id='roomList'></div>"
"  </div>"
"</div>"
"<div>"
"  <div class='panel'><h2>⚡ 4. Danh Sách Thiết Bị Điện & Điều Khiển Thời Gian Thực</h2>"
"    <div id='devGrid' class='dev-grid'></div>"
"  </div>"
"</div>"
"</div>"
"<div id='toast' class='toast'></div>"
"<script>"
"let S={houses:[],rooms:[],devices:[]};"
"function msg(t,err){const el=document.getElementById('toast');el.style.background=err?'#dc2626':'#0284c7';el.innerText=t;el.style.display='block';setTimeout(()=>el.style.display='none',3200);}"
"async function api(u){const r=await fetch(u);return await r.json();}"
"async function loadState(){"
"  try{S=await api('/api/state');render();}catch(e){console.error(e);}"
"}"
"function render(){"
"  document.getElementById('appId').innerText=S.app_id;"
"  let hHtml='',hOpt='';"
"  S.houses.forEach(h=>{"
"    hHtml+=`<div class='stat-line'><span><b>#${h.house_id}</b> ${h.name}</span><button class='btn-sm btn-err' onclick='delHouse(${h.house_id})'>Xóa</button></div>`;"
"    hOpt+=`<option value='${h.house_id}'>#${h.house_id} - ${h.name}</option>`;"
"  });"
"  document.getElementById('houseList').innerHTML=hHtml||'<i>Chưa có căn nhà nào</i>';"
"  const sel=document.getElementById('roomHouseSel');const oldV=sel.value;sel.innerHTML=hOpt;if(oldV)sel.value=oldV;"
"  let rHtml='';"
"  S.rooms.forEach(r=>{"
"    const h=S.houses.find(x=>x.house_id===r.house_id);"
"    rHtml+=`<div class='stat-line'><span><b>Phòng #${r.room_id}:</b> ${r.name} <small>(${h?h.name:''})</small></span><button class='btn-sm btn-err' onclick='delRoom(${r.room_id})'>Xóa</button></div>`;"
"  });"
"  document.getElementById('roomList').innerHTML=rHtml||'<i>Chưa có phòng nào</i>';"
"  let dHtml='';"
"  S.devices.forEach(d=>{"
"    const r=S.rooms.find(x=>x.room_id===d.room_id);"
"    const icon=d.type==='LIGHT'?'💡':(d.type==='FAN'?'🌀':'❄️');"
"    const isOn=d.power==='ON';"
"    dHtml+=`<div class='dev-card ${isOn?'on':''}'>"
"      <div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:8px'>"
"        <div><b style='font-size:1.05rem'>${icon} ${d.name}</b> <small style='color:#38bdf8'>(${d.dev_id})</small><div style='font-size:.78rem;color:#94a3b8'>📍 ${r?r.name:'Phòng #'+d.room_id} | 🌐 ${d.ip}:${d.tcp_port}</div></div>"
"        <span class='badge ${!d.online?'b-err':(isOn?'b-on':'b-off')}'>${!d.online?(d.authenticated?'OFFLINE':'CHƯA XÁC THỰC'):d.power}</span>"
"      </div>`;"
"    if(!d.authenticated){"
"      dHtml+=`<div class='row' style='margin-top:10px'><input type='password' id='pw_${d.dev_id}' placeholder='Mật khẩu (mặc định 123456)' value='123456'><button class='btn-warn btn-sm' onclick='authDev(\"${d.dev_id}\")'>🔑 Kết nối & Xác thực</button></div>`;"
"    }else{"
"      dHtml+=`<div class='stat-line'><span>Công suất hiện tại:</span><b style='color:#facc15'>${d.watt.toFixed(1)} W</b></div>"
"      <div class='stat-line'><span>Tổng điện năng tiêu thụ:</span><b style='color:#38bdf8'>${d.total_wh.toFixed(4)} Wh (${(d.total_wh/1000).toFixed(6)} kWh)</b></div>"
"      <div class='stat-line'><span>TG hoạt động (Ngày/Tháng/Năm):</span><b>${d.day_sec}s / ${d.month_sec}s / ${d.year_sec}s</b></div>"
"      <div class='stat-line'><span>Hẹn giờ tự động:</span><b>${d.timer_rem>0?(d.timer_rem+'s -> '+d.timer_act):'Không đặt'}</b></div>"
"      <div class='row' style='margin-top:10px'>"
"        <button class='btn-ok btn-sm' onclick='sendCmd(\"${d.dev_id}\",\"SET_POWER|ON\")'>BẬT (ON)</button>"
"        <button class='btn-err btn-sm' onclick='sendCmd(\"${d.dev_id}\",\"SET_POWER|OFF\")'>TẮT (OFF)</button>"
"      </div>`;"
"      if(d.type==='FAN'){"
"        dHtml+=`<div class='row'><span style='font-size:.82rem;align-self:center'>Tốc độ gió (${d.fan_speed}):</span>"
"          <button class='btn-sm' onclick='sendCmd(\"${d.dev_id}\",\"SET_FAN|1\")'>Mức 1 (30W)</button>"
"          <button class='btn-sm' onclick='sendCmd(\"${d.dev_id}\",\"SET_FAN|2\")'>Mức 2 (50W)</button>"
"          <button class='btn-sm' onclick='sendCmd(\"${d.dev_id}\",\"SET_FAN|3\")'>Mức 3 (75W)</button></div>`;"
"      }"
"      if(d.type==='AC'){"
"        dHtml+=`<div class='row'>"
"          <select id='acm_${d.dev_id}'><option value='COOL' ${d.ac_mode==='COOL'?'selected':''}>COOL (Làm mát)</option><option value='HEAT' ${d.ac_mode==='HEAT'?'selected':''}>HEAT (Làm nóng)</option><option value='DRY' ${d.ac_mode==='DRY'?'selected':''}>DRY (Hút ẩm)</option></select>"
"          <input type='number' id='act_${d.dev_id}' min='18' max='30' value='${d.ac_temp||24}' style='max-width:70px'>"
"          <button class='btn-sm' onclick='setAc(\"${d.dev_id}\")'>Đặt ĐH</button></div>`;"
"      }"
"      dHtml+=`<div class='row'>"
"        <input type='number' id='tm_${d.dev_id}' placeholder='Giây (vd 30)' style='max-width:95px'>"
"        <select id='tma_${d.dev_id}'><option value='OFF'>Tự TẮT</option><option value='ON'>Tự BẬT</option></select>"
"        <button class='btn-sm btn-warn' onclick='setTimer(\"${d.dev_id}\")'>⏲️ Hẹn giờ</button>"
"      </div>`;"
"    }"
"    dHtml+=`<div style='display:flex;justify-content:space-between;margin-top:8px;padding-top:6px;border-top:1px solid #1e293b'>"
"      <button class='btn-sm' style='background:#334155' onclick='changePass(\"${d.dev_id}\")'>🔒 Đổi mật khẩu</button>"
"      <button class='btn-sm btn-err' onclick='delDev(\"${d.dev_id}\")'>🗑️ Xóa khỏi phòng</button>"
"    </div></div>`;"
"  });"
"  document.getElementById('devGrid').innerHTML=dHtml||'<i>Chưa có thiết bị nào trong phòng. Hãy nhấn <b>Quét Ngay</b> ở cột bên trái để thêm thiết bị!</i>';"
"}"
"async function scanDevs(){"
"  const ip=document.getElementById('scanIp').value;"
"  document.getElementById('scanList').innerHTML='⏳ Đang phát bản tin UDP Broadcast (cổng 9999)...';"
"  const res=await api('/api/scan?ip='+encodeURIComponent(ip));"
"  if(!res.devices||res.devices.length===0){document.getElementById('scanList').innerHTML='❌ Không tìm thấy thiết bị nào.';return;}"
"  let rOpt='';S.rooms.forEach(r=>rOpt+=`<option value='${r.room_id}'>Phòng #${r.room_id}: ${r.name}</option>`);"
"  let html='';"
"  res.devices.forEach(d=>{"
"    html+=`<div class='scan-item'><div><b>${d.dev_id}</b> [${d.type}] - ${d.name}</div>"
"    <div style='color:#38bdf8;margin:3px 0'>IP: ${d.ip}:${d.tcp_port}</div>"
"    <div class='row' style='margin:4px 0 0'><select id='sr_${d.dev_id}'>${rOpt}</select>"
"    <button class='btn-sm btn-ok' onclick='addDevToRoom(\"${d.dev_id}\",\"${d.type}\",\"${d.name}\",\"${d.ip}\",${d.tcp_port})'>+ Thêm vào phòng</button></div></div>`;"
"  });"
"  document.getElementById('scanList').innerHTML=html;"
"}"
"async function addHouse(){const n=document.getElementById('houseName').value;if(!n)return;await api('/api/house/add?name='+encodeURIComponent(n));document.getElementById('houseName').value='';loadState();}"
"async function delHouse(id){await api('/api/house/delete?id='+id);loadState();}"
"async function addRoom(){const hid=document.getElementById('roomHouseSel').value;const n=document.getElementById('roomName').value;if(!hid||!n)return;await api(`/api/room/add?house_id=${hid}&name=${encodeURIComponent(n)}`);document.getElementById('roomName').value='';loadState();}"
"async function delRoom(id){await api('/api/room/delete?id='+id);loadState();}"
"async function addDevToRoom(id,t,n,ip,p){const rid=document.getElementById('sr_'+id).value;if(!rid){msg('Vui lòng tạo Phòng trước!',true);return;}const r=await api(`/api/device/add?room_id=${rid}&dev_id=${encodeURIComponent(id)}&type=${t}&name=${encodeURIComponent(n)}&ip=${ip}&port=${p}`);msg(r.msg,!r.ok);loadState();}"
"async function delDev(id){await api('/api/device/delete?dev_id='+encodeURIComponent(id));loadState();}"
"async function authDev(id){const pw=document.getElementById('pw_'+id).value;const r=await api(`/api/device/auth?dev_id=${encodeURIComponent(id)}&pass=${encodeURIComponent(pw)}`);msg(r.msg,!r.ok);loadState();}"
"async function sendCmd(id,cmd){const r=await api(`/api/device/cmd?dev_id=${encodeURIComponent(id)}&cmd=${encodeURIComponent(cmd)}`);msg(r.msg,!r.ok);loadState();}"
"function setAc(id){const m=document.getElementById('acm_'+id).value;const t=document.getElementById('act_'+id).value;sendCmd(id,`SET_AC|${m}|${t}`);}"
"function setTimer(id){const s=document.getElementById('tm_'+id).value||0;const a=document.getElementById('tma_'+id).value;sendCmd(id,`SET_TIMER|${s}|${a}`);}"
"async function changePass(id){const o=prompt('Nhập mật khẩu cũ:','123456');if(!o)return;const n=prompt('Nhập mật khẩu mới:');if(!n)return;const r=await api(`/api/device/passwd?dev_id=${encodeURIComponent(id)}&old_pass=${encodeURIComponent(o)}&new_pass=${encodeURIComponent(n)}`);msg(r.msg,!r.ok);loadState();}"
"loadState();setInterval(loadState,3000);"
"</script></body></html>";

/* ==================== CÁC HÀM XỬ LÝ API HTTP ==================== */

void send_http_response(int client_fd, const char *content_type, const char *body) {
    char header[512];
    size_t body_len = strlen(body);
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        content_type, body_len);
    send(client_fd, header, hlen, 0);
    send(client_fd, body, body_len, 0);
}

void handle_api_state(int client_fd) {
    home_load(&g_app, DB_FILE);

    char *json = malloc(65536);
    if (!json) return;
    int offset = 0;

    offset += snprintf(json + offset, 65536 - offset,
                       "{\"app_id\":\"%s\",\"houses\":[", g_app.app_id);

    int first = 1;
    for (int i = 0; i < MAX_HOUSES; i++) {
        if (g_app.houses[i].used) {
            char ename[128];
            json_escape(g_app.houses[i].name, ename, sizeof(ename));
            offset += snprintf(json + offset, 65536 - offset,
                               "%s{\"house_id\":%d,\"name\":\"%s\"}",
                               first ? "" : ",", g_app.houses[i].house_id, ename);
            first = 0;
        }
    }

    offset += snprintf(json + offset, 65536 - offset, "],\"rooms\":[");
    first = 1;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (g_app.rooms[i].used) {
            char ename[128];
            json_escape(g_app.rooms[i].name, ename, sizeof(ename));
            offset += snprintf(json + offset, 65536 - offset,
                               "%s{\"room_id\":%d,\"house_id\":%d,\"name\":\"%s\"}",
                               first ? "" : ",", g_app.rooms[i].room_id,
                               g_app.rooms[i].house_id, ename);
            first = 0;
        }
    }

    offset += snprintf(json + offset, 65536 - offset, "],\"devices\":[");
    first = 1;
    for (int i = 0; i < MAX_USER_DEVICES; i++) {
        if (!g_app.devices[i].used) continue;
        ManagedDevice *d = &g_app.devices[i];

        int auth = (strlen(d->token) > 0);
        int online = 0;
        char power[16] = "OFF";
        double watt = 0.0, total_wh = 0.0;
        int fan_speed = 1, ac_temp = 24, timer_rem = 0;
        char ac_mode[16] = "COOL", timer_act[16] = "OFF";
        long day_sec = 0, month_sec = 0, year_sec = 0;

        if (auth) {
            char cmd[128], resp[MAX_LINE_LEN];
            snprintf(cmd, sizeof(cmd), "GET_INFO|%s", d->token);
            if (send_device_tcp_cmd_fast(d->ip, d->tcp_port, cmd, resp, sizeof(resp), 400) == 0) {
                if (strncmp(resp, "100|INFO|", 9) == 0) {
                    online = 1;
                    char *fields[20];
                    int fc = 0;
                    char *saveptr = NULL;
                    char *tok = strtok_r(resp, "|", &saveptr);
                    while (tok && fc < 20) {
                        fields[fc++] = tok;
                        tok = strtok_r(NULL, "|", &saveptr);
                    }
                    if (fc >= 16) {
                        strncpy(power, fields[5], sizeof(power) - 1);
                        watt      = atof(fields[6]);
                        fan_speed = atoi(fields[7]);
                        strncpy(ac_mode, fields[8], sizeof(ac_mode) - 1);
                        ac_temp   = atoi(fields[9]);
                        timer_rem = atoi(fields[10]);
                        strncpy(timer_act, fields[11], sizeof(timer_act) - 1);
                        day_sec   = atol(fields[12]);
                        month_sec = atol(fields[13]);
                        year_sec  = atol(fields[14]);
                        total_wh  = atof(fields[15]);
                    }
                } else if (strncmp(resp, "203|", 4) == 0) {
                    d->token[0] = '\0';
                    auth = 0;
                    home_save(&g_app, DB_FILE);
                }
            }
        }

        char ename[128];
        json_escape(d->name, ename, sizeof(ename));
        offset += snprintf(json + offset, 65536 - offset,
            "%s{\"dev_id\":\"%s\",\"room_id\":%d,\"type\":\"%s\",\"name\":\"%s\","
            "\"ip\":\"%s\",\"tcp_port\":%d,\"authenticated\":%s,\"online\":%s,"
            "\"power\":\"%s\",\"watt\":%.2f,\"fan_speed\":%d,\"ac_mode\":\"%s\","
            "\"ac_temp\":%d,\"timer_rem\":%d,\"timer_act\":\"%s\","
            "\"day_sec\":%ld,\"month_sec\":%ld,\"year_sec\":%ld,\"total_wh\":%.4f}",
            first ? "" : ",",
            d->dev_id, d->room_id, device_type_to_str(d->type), ename,
            d->ip, d->tcp_port, auth ? "true" : "false", online ? "true" : "false",
            power, watt, fan_speed, ac_mode, ac_temp, timer_rem, timer_act,
            day_sec, month_sec, year_sec, total_wh);
        first = 0;
    }

    offset += snprintf(json + offset, 65536 - offset, "]}");
    send_http_response(client_fd, "application/json", json);
    free(json);
}

void handle_api_scan(int client_fd, const char *query) {
    char custom_ip[64] = {0};
    get_query_param(query, "ip", custom_ip, sizeof(custom_ip));

    int udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock < 0) {
        send_http_response(client_fd, "application/json", "{\"devices\":[]}");
        return;
    }

    int broadcast_enable = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    const char *scan_msg = "SCAN_REQ\r\n";
    struct sockaddr_in bcast_addr;
    memset(&bcast_addr, 0, sizeof(bcast_addr));
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port   = htons(DISCOVERY_UDP_PORT);

    const char *targets[] = { "255.255.255.255", "127.255.255.255", "192.168.4.255", "192.168.4.1" };
    for (int i = 0; i < 4; i++) {
        bcast_addr.sin_addr.s_addr = inet_addr(targets[i]);
        sendto(udp_sock, scan_msg, strlen(scan_msg), 0,
               (struct sockaddr *)&bcast_addr, sizeof(bcast_addr));
    }

    if (strlen(custom_ip) > 0) {
        bcast_addr.sin_addr.s_addr = inet_addr(custom_ip);
        sendto(udp_sock, scan_msg, strlen(scan_msg), 0,
               (struct sockaddr *)&bcast_addr, sizeof(bcast_addr));
    }

    ScannedDevice found[MAX_SCANNED_DEVS];
    int found_count = 0;
    struct pollfd pfd = { .fd = udp_sock, .events = POLLIN };

    while (found_count < MAX_SCANNED_DEVS) {
        int ret = poll(&pfd, 1, 800);
        if (ret <= 0) break;

        if (pfd.revents & POLLIN) {
            char buf[512];
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            ssize_t n = recvfrom(udp_sock, buf, sizeof(buf) - 1, 0,
                                 (struct sockaddr *)&from_addr, &from_len);
            if (n > 0) {
                buf[n] = '\0';
                buf[strcspn(buf, "\r\n")] = '\0';

                char *saveptr = NULL;
                char *tag   = strtok_r(buf, "|", &saveptr);
                char *did   = strtok_r(NULL, "|", &saveptr);
                char *tstr  = strtok_r(NULL, "|", &saveptr);
                char *pstr  = strtok_r(NULL, "|", &saveptr);
                char *dname = strtok_r(NULL, "|", &saveptr);

                if (tag && strcmp(tag, "SCAN_RES") == 0 && did && tstr && pstr) {
                    int dup = 0;
                    for (int i = 0; i < found_count; i++) {
                        if (strcasecmp(found[i].dev_id, did) == 0) { dup = 1; break; }
                    }
                    if (!dup) {
                        strncpy(found[found_count].dev_id, did, ID_LEN - 1);
                        found[found_count].type = str_to_device_type(tstr);
                        strncpy(found[found_count].name, dname ? dname : did, NAME_LEN - 1);
                        strncpy(found[found_count].ip, inet_ntoa(from_addr.sin_addr), 63);
                        found[found_count].tcp_port = atoi(pstr);
                        found_count++;
                    }
                }
            }
        }
    }
    close(udp_sock);

    char json[8192];
    int offset = snprintf(json, sizeof(json), "{\"devices\":[");
    for (int i = 0; i < found_count; i++) {
        char ename[128];
        json_escape(found[i].name, ename, sizeof(ename));
        offset += snprintf(json + offset, sizeof(json) - offset,
            "%s{\"dev_id\":\"%s\",\"type\":\"%s\",\"name\":\"%s\",\"ip\":\"%s\",\"tcp_port\":%d}",
            (i == 0) ? "" : ",", found[i].dev_id, device_type_to_str(found[i].type),
            ename, found[i].ip, found[i].tcp_port);
    }
    snprintf(json + offset, sizeof(json) - offset, "]}");
    send_http_response(client_fd, "application/json", json);
}

/* ==================== VÒNG LẶP HTTP SERVER TRUNG TÂM ==================== */

void handle_http_client(int client_fd) {
    char req[2048];
    ssize_t n = recv(client_fd, req, sizeof(req) - 1, 0);
    if (n <= 0) return;
    req[n] = '\0';

    char method[16] = {0}, uri[1024] = {0};
    if (sscanf(req, "%15s %1023s", method, uri) != 2) return;

    char *query = strchr(uri, '?');
    if (query) {
        *query = '\0';
        query++;
    } else {
        query = "";
    }

    if (strcmp(uri, "/") == 0) {
        send_http_response(client_fd, "text/html", DASHBOARD_HTML);
    } else if (strcmp(uri, "/api/state") == 0) {
        handle_api_state(client_fd);
    } else if (strcmp(uri, "/api/scan") == 0) {
        handle_api_scan(client_fd, query);
    } else if (strcmp(uri, "/api/house/add") == 0) {
        home_load(&g_app, DB_FILE);
        char name[NAME_LEN];
        if (get_query_param(query, "name", name, sizeof(name)) && strlen(name) > 0) {
            house_add(&g_app, name);
            home_save(&g_app, DB_FILE);
        }
        send_http_response(client_fd, "application/json", "{\"ok\":true}");
    } else if (strcmp(uri, "/api/house/delete") == 0) {
        home_load(&g_app, DB_FILE);
        char id_str[32];
        if (get_query_param(query, "id", id_str, sizeof(id_str))) {
            house_delete(&g_app, atoi(id_str));
            home_save(&g_app, DB_FILE);
        }
        send_http_response(client_fd, "application/json", "{\"ok\":true}");
    } else if (strcmp(uri, "/api/room/add") == 0) {
        home_load(&g_app, DB_FILE);
        char hid_str[32], name[NAME_LEN];
        if (get_query_param(query, "house_id", hid_str, sizeof(hid_str)) &&
            get_query_param(query, "name", name, sizeof(name)) && strlen(name) > 0) {
            room_add(&g_app, atoi(hid_str), name);
            home_save(&g_app, DB_FILE);
        }
        send_http_response(client_fd, "application/json", "{\"ok\":true}");
    } else if (strcmp(uri, "/api/room/delete") == 0) {
        home_load(&g_app, DB_FILE);
        char id_str[32];
        if (get_query_param(query, "id", id_str, sizeof(id_str))) {
            room_delete(&g_app, atoi(id_str));
            home_save(&g_app, DB_FILE);
        }
        send_http_response(client_fd, "application/json", "{\"ok\":true}");
    } else if (strcmp(uri, "/api/device/add") == 0) {
        home_load(&g_app, DB_FILE);
        char rid[32], did[ID_LEN], type[32], name[NAME_LEN], ip[64], port[32];
        if (get_query_param(query, "room_id", rid, sizeof(rid)) &&
            get_query_param(query, "dev_id", did, sizeof(did)) &&
            get_query_param(query, "type", type, sizeof(type)) &&
            get_query_param(query, "name", name, sizeof(name)) &&
            get_query_param(query, "ip", ip, sizeof(ip)) &&
            get_query_param(query, "port", port, sizeof(port))) {
            int res = device_add_to_room(&g_app, atoi(rid), did, str_to_device_type(type), name, ip, atoi(port));
            home_save(&g_app, DB_FILE);
            if (res == 0)
                send_http_response(client_fd, "application/json", "{\"ok\":true,\"msg\":\"Đã thêm thiết bị vào phòng!\"}");
            else
                send_http_response(client_fd, "application/json", "{\"ok\":false,\"msg\":\"Thiết bị đã tồn tại hoặc lỗi phòng!\"}");
        } else {
            send_http_response(client_fd, "application/json", "{\"ok\":false,\"msg\":\"Thiếu tham số!\"}");
        }
    } else if (strcmp(uri, "/api/device/delete") == 0) {
        home_load(&g_app, DB_FILE);
        char did[ID_LEN];
        if (get_query_param(query, "dev_id", did, sizeof(did))) {
            device_remove(&g_app, did);
            home_save(&g_app, DB_FILE);
        }
        send_http_response(client_fd, "application/json", "{\"ok\":true}");
    } else if (strcmp(uri, "/api/device/auth") == 0) {
        home_load(&g_app, DB_FILE);
        char did[ID_LEN], pass[PASS_LEN];
        get_query_param(query, "dev_id", did, sizeof(did));
        get_query_param(query, "pass", pass, sizeof(pass));
        ManagedDevice *d = device_find(&g_app, did);
        if (!d) {
            send_http_response(client_fd, "application/json", "{\"ok\":false,\"msg\":\"Không tìm thấy thiết bị!\"}");
            return;
        }
        char cmd[256], resp[MAX_LINE_LEN];
        snprintf(cmd, sizeof(cmd), "AUTH|%s|%s", g_app.app_id, pass);
        if (send_device_tcp_cmd_fast(d->ip, d->tcp_port, cmd, resp, sizeof(resp), 2000) == 0 &&
            strncmp(resp, "100|AUTH_OK|", 12) == 0) {
            char *saveptr = NULL;
            strtok_r(resp, "|", &saveptr);
            strtok_r(NULL, "|", &saveptr);
            char *token = strtok_r(NULL, "|", &saveptr);
            if (token) {
                strncpy(d->token, token, TOKEN_LEN - 1);
                home_save(&g_app, DB_FILE);
                send_http_response(client_fd, "application/json", "{\"ok\":true,\"msg\":\"Xác thực thành công! Đã lưu Token.\"}");
                return;
            }
        }
        send_http_response(client_fd, "application/json", "{\"ok\":false,\"msg\":\"Sai mật khẩu hoặc mất kết nối TCP!\"}");
    } else if (strcmp(uri, "/api/device/passwd") == 0) {
        home_load(&g_app, DB_FILE);
        char did[ID_LEN], old_p[PASS_LEN], new_p[PASS_LEN];
        get_query_param(query, "dev_id", did, sizeof(did));
        get_query_param(query, "old_pass", old_p, sizeof(old_p));
        get_query_param(query, "new_pass", new_p, sizeof(new_p));
        ManagedDevice *d = device_find(&g_app, did);
        if (!d || strlen(d->token) == 0) {
            send_http_response(client_fd, "application/json", "{\"ok\":false,\"msg\":\"Chưa xác thực thiết bị!\"}");
            return;
        }
        char cmd[256], resp[MAX_LINE_LEN];
        snprintf(cmd, sizeof(cmd), "PASSWD|%s|%s|%s", d->token, old_p, new_p);
        if (send_device_tcp_cmd_fast(d->ip, d->tcp_port, cmd, resp, sizeof(resp), 2000) == 0 &&
            strncmp(resp, "100|PASSWD_OK", 13) == 0) {
            send_http_response(client_fd, "application/json", "{\"ok\":true,\"msg\":\"Đổi mật khẩu thiết bị thành công!\"}");
        } else {
            send_http_response(client_fd, "application/json", "{\"ok\":false,\"msg\":\"Đổi mật khẩu thất bại!\"}");
        }
    } else if (strcmp(uri, "/api/device/cmd") == 0) {
        home_load(&g_app, DB_FILE);
        char did[ID_LEN], raw_cmd[128];
        get_query_param(query, "dev_id", did, sizeof(did));
        get_query_param(query, "cmd", raw_cmd, sizeof(raw_cmd));
        ManagedDevice *d = device_find(&g_app, did);
        if (!d || strlen(d->token) == 0) {
            send_http_response(client_fd, "application/json", "{\"ok\":false,\"msg\":\"Thiết bị chưa được xác thực!\"}");
            return;
        }
        /* Tách tên lệnh đầu và ghép token vào giữa: vd SET_POWER|ON -> SET_POWER|<token>|ON */
        char *bar = strchr(raw_cmd, '|');
        if (!bar) {
            send_http_response(client_fd, "application/json", "{\"ok\":false,\"msg\":\"Lệnh không hợp lệ!\"}");
            return;
        }
        *bar = '\0';
        const char *cmd_name = raw_cmd;
        const char *cmd_args = bar + 1;

        char full_cmd[256], resp[MAX_LINE_LEN];
        snprintf(full_cmd, sizeof(full_cmd), "%s|%s|%s", cmd_name, d->token, cmd_args);
        if (send_device_tcp_cmd_fast(d->ip, d->tcp_port, full_cmd, resp, sizeof(resp), 2000) == 0 &&
            strncmp(resp, "100|", 4) == 0) {
            char out[512], eresp[256];
            json_escape(resp, eresp, sizeof(eresp));
            snprintf(out, sizeof(out), "{\"ok\":true,\"msg\":\"Lệnh thành công: %s\"}", eresp);
            send_http_response(client_fd, "application/json", out);
        } else {
            send_http_response(client_fd, "application/json", "{\"ok\":false,\"msg\":\"Thiết bị từ chối hoặc mất kết nối!\"}");
        }
    } else {
        send_http_response(client_fd, "text/plain", "404 Not Found");
    }
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_WEB_PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
    }

    home_init(&g_app, DB_FILE);

    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        perror("socket() failed");
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind() failed");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 16) < 0) {
        perror("listen() failed");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("==============================================================\n");
    printf("   SMART HOME WEB GATEWAY DASHBOARD (IT4062)\n");
    printf("   - App ID       : %s\n", g_app.app_id);
    printf("   - Database     : %s (Dong bo voi user_client CLI)\n", DB_FILE);
    printf("   - Web URL      : http://localhost:%d\n", port);
    printf("==============================================================\n");
    fflush(stdout);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        handle_http_client(client_fd);
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
