#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <poll.h>

#include "../common/protocol.h"
#include "../common/net_utils.h"
#include "home_manager.h"

#define DB_FILE "client_data.db"

static UserAppData g_app;

/* Đọc 1 dòng từ bàn phím an toàn */
void read_input_line(const char *prompt, char *buf, size_t size) {
    if (prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }
    if (fgets(buf, (int)size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
}

int read_input_int(const char *prompt) {
    char buf[64];
    read_input_line(prompt, buf, sizeof(buf));
    return atoi(buf);
}

/* Tạo kết nối TCP tới thiết bị và gửi 1 lệnh, nhận 1 dòng phản hồi */
int send_device_tcp_command(const char *ip, int port, const char *cmd_line, char *resp_buf, size_t resp_size) {
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        perror("socket() failed");
        return -1;
    }

    /* Thiết lập timeout nhận dữ liệu 3 giây để tránh treo nếu thiết bị tắt */
    struct timeval tv;
    tv.tv_sec  = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        printf("[LOI] Dia chi IP khong hop le: %s\n", ip);
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("[LOI] Khong the ket noi TCP toi thiet bi tai %s:%d (%s)\n",
               ip, port, strerror(errno));
        close(sock);
        return -1;
    }

    if (send_line(sock, cmd_line) < 0) {
        printf("[LOI] Gui lenh that bai!\n");
        close(sock);
        return -1;
    }

    int n = recv_line(sock, resp_buf, resp_size);
    close(sock);

    if (n <= 0) {
        printf("[LOI] Thiet bi khong phan hoi!\n");
        return -1;
    }
    return 0;
}

/* Quy đổi tổng số giây sang định dạng Giờ : Phút : Giây */
void format_duration(long total_sec, char *out, size_t out_size) {
    long hrs  = total_sec / 3600;
    long mins = (total_sec % 3600) / 60;
    long secs = total_sec % 60;
    snprintf(out, out_size, "%02ld gio %02ld phut %02ld giay (%ld s)", hrs, mins, secs, total_sec);
}

/* ==================== CHỨC NĂNG QUÉT THIẾT BỊ (UDP BROADCAST) ==================== */

void menu_scan_devices(void) {
    printf("\n==================== QUET THIET BI TREN MANG (UDP) ====================\n");
    char custom_ip[64] = {0};
    read_input_line("Nhap IP/Broadcast mang Wi-Fi (Bấm Enter de quet tự động toàn mạng LAN): ",
                    custom_ip, sizeof(custom_ip));

    int udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock < 0) {
        perror("socket(UDP) failed");
        return;
    }

    int broadcast_enable = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    const char *scan_msg = "SCAN_REQ\r\n";

    /* 1. Gửi tới địa chỉ Broadcast toàn cục 255.255.255.255:9999 */
    struct sockaddr_in bcast_addr;
    memset(&bcast_addr, 0, sizeof(bcast_addr));
    bcast_addr.sin_family      = AF_INET;
    bcast_addr.sin_port        = htons(DISCOVERY_UDP_PORT);
    bcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
    sendto(udp_sock, scan_msg, strlen(scan_msg), 0,
           (struct sockaddr *)&bcast_addr, sizeof(bcast_addr));

    /* 2. Gửi tới Loopback Broadcast 127.255.255.255:9999 */
    bcast_addr.sin_addr.s_addr = inet_addr("127.255.255.255");
    sendto(udp_sock, scan_msg, strlen(scan_msg), 0,
           (struct sockaddr *)&bcast_addr, sizeof(bcast_addr));

    /* 3. Nếu người dùng nhập IP của ESP32 hoặc Broadcast subnet (vd 192.168.1.255) */
    if (strlen(custom_ip) > 0) {
        bcast_addr.sin_addr.s_addr = inet_addr(custom_ip);
        sendto(udp_sock, scan_msg, strlen(scan_msg), 0,
               (struct sockaddr *)&bcast_addr, sizeof(bcast_addr));
    }

    printf("Dang lang nghe phan hoi tu cac thiet bi trong 1.5 giay...\n");

    ScannedDevice found[MAX_SCANNED_DEVS];
    int found_count = 0;

    struct pollfd pfd;
    pfd.fd     = udp_sock;
    pfd.events = POLLIN;

    /* Lắng nghe phản hồi bằng poll() */
    while (found_count < MAX_SCANNED_DEVS) {
        int ret = poll(&pfd, 1, 1500);
        if (ret <= 0) break; /* Hết thời gian chờ */

        if (pfd.revents & POLLIN) {
            char buf[512];
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            ssize_t n = recvfrom(udp_sock, buf, sizeof(buf) - 1, 0,
                                 (struct sockaddr *)&from_addr, &from_len);
            if (n > 0) {
                buf[n] = '\0';
                buf[strcspn(buf, "\r\n")] = '\0';

                /* Phân tích: SCAN_RES|<dev_id>|<type>|<tcp_port>|<dev_name> */
                char *saveptr = NULL;
                char *tag     = strtok_r(buf, "|", &saveptr);
                char *did     = strtok_r(NULL, "|", &saveptr);
                char *tstr    = strtok_r(NULL, "|", &saveptr);
                char *pstr    = strtok_r(NULL, "|", &saveptr);
                char *dname   = strtok_r(NULL, "|", &saveptr);

                if (tag && strcmp(tag, "SCAN_RES") == 0 && did && tstr && pstr) {
                    /* Kiểm tra trùng lặp ID */
                    int duplicate = 0;
                    for (int i = 0; i < found_count; i++) {
                        if (strcasecmp(found[i].dev_id, did) == 0) {
                            duplicate = 1;
                            break;
                        }
                    }
                    if (!duplicate) {
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

    if (found_count == 0) {
        printf("\n[!] Khong tim thay thiet bi nao tren mang.\n");
        return;
    }

    printf("\nTim thay %d thiet bi:\n", found_count);
    printf("+-----+--------------+--------+------------------------+-----------------+-------+\n");
    printf("| STT | Ma TB (ID)   | Loai   | Ten Thiet Bi           | Dia chi IP      | Port  |\n");
    printf("+-----+--------------+--------+------------------------+-----------------+-------+\n");
    for (int i = 0; i < found_count; i++) {
        printf("| %-3d | %-12s | %-6s | %-22s | %-15s | %-5d |\n",
               i + 1,
               found[i].dev_id,
               device_type_to_str(found[i].type),
               found[i].name,
               found[i].ip,
               found[i].tcp_port);
    }
    printf("+-----+--------------+--------+------------------------+-----------------+-------+\n");

    int choice = read_input_int("\nNhap STT thiet bi muon them vao Phong (nhap 0 de bo qua): ");
    if (choice < 1 || choice > found_count) return;

    ScannedDevice *sel = &found[choice - 1];
    room_list_all(&g_app);
    int room_id = read_input_int("Nhap Ma Phong (Room ID) muon dat thiet bi nay: ");
    if (room_find(&g_app, room_id) == NULL) {
        printf("[LOI] Ma phong khong ton tai!\n");
        return;
    }

    if (device_add_to_room(&g_app, room_id, sel->dev_id, sel->type, sel->name, sel->ip, sel->tcp_port) == 0) {
        home_save(&g_app, DB_FILE);
        printf("[THANH CONG] Da them thiet bi '%s' (%s) vao Phong ID=%d!\n",
               sel->name, sel->dev_id, room_id);
    }
}

/* ==================== KẾT NỐI & XÁC THỰC THIẾT BỊ ==================== */

int authenticate_device(ManagedDevice *dev) {
    char pass[PASS_LEN];
    char prompt[256];
    snprintf(prompt, sizeof(prompt), "Nhap mat khau cua thiet bi '%s' (%s): ", dev->name, dev->dev_id);
    read_input_line(prompt, pass, sizeof(pass));

    char cmd[MAX_LINE_LEN];
    char resp[MAX_LINE_LEN];
    snprintf(cmd, sizeof(cmd), "AUTH|%s|%s", g_app.app_id, pass);

    if (send_device_tcp_command(dev->ip, dev->tcp_port, cmd, resp, sizeof(resp)) < 0) {
        return -1;
    }

    /* Phản hồi: 100|AUTH_OK|<token>|<id>|<type>|<name>|<power>|<watt> */
    char *saveptr = NULL;
    char *code_str = strtok_r(resp, "|", &saveptr);
    char *msg      = strtok_r(NULL, "|", &saveptr);
    char *token    = strtok_r(NULL, "|", &saveptr);

    if (code_str && atoi(code_str) == STATUS_OK && token) {
        strncpy(dev->token, token, TOKEN_LEN - 1);
        home_save(&g_app, DB_FILE);
        printf("[XAC THUC THANH CONG] Da nhan Token=%s gan voi AppID=%s!\n", dev->token, g_app.app_id);
        return 0;
    } else {
        printf("[XAC THUC THAT BAI] Phan hoi tu thiet bi: %s (%s)\n",
               code_str ? code_str : "ERR", msg ? msg : "Sai mat khau");
        return -1;
    }
}

/* ==================== HIỂN THỊ THÔNG TIN CHI TIẾT THIẾT BỊ ==================== */

void show_device_detailed_info(ManagedDevice *dev) {
    char cmd[MAX_LINE_LEN];
    char resp[MAX_LINE_LEN];
    snprintf(cmd, sizeof(cmd), "GET_INFO|%s", dev->token);

    if (send_device_tcp_command(dev->ip, dev->tcp_port, cmd, resp, sizeof(resp)) < 0) {
        return;
    }

    char *saveptr = NULL;
    char *code_str = strtok_r(resp, "|", &saveptr);
    char *tag      = strtok_r(NULL, "|", &saveptr);

    if (!code_str || atoi(code_str) != STATUS_OK || !tag) {
        printf("[LOI] Khong lay duoc thong tin (%s). Co the Token da het han, vui long ket noi lai!\n",
               tag ? tag : "UNAUTHORIZED");
        dev->token[0] = '\0';
        home_save(&g_app, DB_FILE);
        return;
    }

    char *id        = strtok_r(NULL, "|", &saveptr);
    char *type      = strtok_r(NULL, "|", &saveptr);
    char *name      = strtok_r(NULL, "|", &saveptr);
    char *power     = strtok_r(NULL, "|", &saveptr);
    char *watt_str  = strtok_r(NULL, "|", &saveptr);
    char *fan_str   = strtok_r(NULL, "|", &saveptr);
    char *ac_mode   = strtok_r(NULL, "|", &saveptr);
    char *ac_temp   = strtok_r(NULL, "|", &saveptr);
    char *tmr_rem   = strtok_r(NULL, "|", &saveptr);
    char *tmr_act   = strtok_r(NULL, "|", &saveptr);
    char *day_s     = strtok_r(NULL, "|", &saveptr);
    char *month_s   = strtok_r(NULL, "|", &saveptr);
    char *year_s    = strtok_r(NULL, "|", &saveptr);
    char *wh_str    = strtok_r(NULL, "|", &saveptr);

    long d_sec = day_s   ? atol(day_s)   : 0;
    long m_sec = month_s ? atol(month_s) : 0;
    long y_sec = year_s  ? atol(year_s)  : 0;
    double wh  = wh_str  ? atof(wh_str)  : 0.0;

    char d_buf[64], m_buf[64], y_buf[64];
    format_duration(d_sec, d_buf, sizeof(d_buf));
    format_duration(m_sec, m_buf, sizeof(m_buf));
    format_duration(y_sec, y_buf, sizeof(y_buf));

    printf("\n+----------------------------------------------------------------------+\n");
    printf("|                THONG TIN CHI TIET & THONG KE DIEN NANG               |\n");
    printf("+----------------------------------------------------------------------+\n");
    printf("  - Ma thiet bi (ID)       : %s\n", id ? id : dev->dev_id);
    printf("  - Ten thiet bi           : %s\n", name ? name : dev->name);
    printf("  - Loai thiet bi          : %s\n", type ? type : device_type_to_str(dev->type));
    printf("  - Dia chi mang           : %s:%d\n", dev->ip, dev->tcp_port);
    printf("  - Trang thai hien tai    : %s\n", (power && strcmp(power, "ON") == 0) ? "DANG BAT (ON)" : "DANG TAT (OFF)");
    printf("  - Cong suat tuc thoi     : %s W\n", watt_str ? watt_str : "0.0");

    if (dev->type == DEV_FAN && fan_str) {
        printf("  - Toc do gio Quat        : Muc %s (1: Nhe=30W, 2: Vua=50W, 3: Manh=75W)\n", fan_str);
    } else if (dev->type == DEV_AC && ac_mode && ac_temp) {
        printf("  - Cau hinh Dieu hoa      : Che do %s | Nhiet do %s do C\n", ac_mode, ac_temp);
    }

    if (tmr_rem && atoi(tmr_rem) > 0 && tmr_act) {
        printf("  - Hen gio dang chay      : Con %s giay nua se chuyen sang %s\n", tmr_rem, tmr_act);
    } else {
        printf("  - Hen gio                : Khong dat\n");
    }

    printf("------------------------------------------------------------------------\n");
    printf("  - Thoi gian chay NGAY    : %s\n", d_buf);
    printf("  - Thoi gian chay THANG   : %s\n", m_buf);
    printf("  - Thoi gian chay NAM     : %s\n", y_buf);
    printf("  - Dien nang tieu thu     : %.4f Wh (%.6f kWh)\n", wh, wh / 1000.0);
    printf("  - Tien dien uoc tinh     : %.2f VND (don gia 3,000 VND/kWh)\n", (wh / 1000.0) * 3000.0);
    printf("+----------------------------------------------------------------------+\n");
}

/* ==================== BẢNG ĐIỀU KHIỂN THIẾT BỊ ==================== */

void control_device_panel(ManagedDevice *dev) {
    /* Nếu chưa có Token thì yêu cầu xác thực mật khẩu trước */
    if (strlen(dev->token) == 0) {
        printf("\n[!] Thiet bi '%s' chua duoc xac thuc. Vui long nhap mat khau de ket noi:\n", dev->name);
        if (authenticate_device(dev) < 0) return;
    }

    while (1) {
        printf("\n================ DIEU KHIEN THIET BI: %s (%s - %s) ================\n",
               dev->name, dev->dev_id, device_type_to_str(dev->type));
        printf("  1. Xem thong tin chi tiet & Thong ke dien nang\n");
        printf("  2. Bat thiet bi (TURN ON)\n");
        printf("  3. Tat thiet bi (TURN OFF)\n");
        printf("  4. Hen gio Bat / Tat (TIMER)\n");
        if (dev->type == DEV_FAN) {
            printf("  5. Chinh toc do gio Quat (Muc 1, 2, 3)\n");
        } else if (dev->type == DEV_AC) {
            printf("  5. Chinh Che do (COOL/HEAT/DRY) & Nhiet do (18-30C) Dieu hoa\n");
        } else {
            printf("  5. [Chuc nang rieng cua Quat / Dieu hoa]\n");
        }
        printf("  6. Doi mat khau cua thiet bi\n");
        printf("  7. Xac thuc lai (Cap lai Token moi)\n");
        printf("  0. Quay lai Menu chinh\n");

        int ch = read_input_int("Chon thao tac: ");
        if (ch == 0) break;

        char cmd[MAX_LINE_LEN];
        char resp[MAX_LINE_LEN];

        if (ch == 1) {
            show_device_detailed_info(dev);
        } else if (ch == 2 || ch == 3) {
            const char *target = (ch == 2) ? "ON" : "OFF";
            snprintf(cmd, sizeof(cmd), "SET_POWER|%s|%s", dev->token, target);
            if (send_device_tcp_command(dev->ip, dev->tcp_port, cmd, resp, sizeof(resp)) == 0) {
                printf("-> Phan hoi tu thiet bi: %s\n", resp);
            }
        } else if (ch == 4) {
            int secs = read_input_int("Nhap so giay hen gio (nhap 0 de huy hen gio): ");
            char act[16] = "OFF";
            if (secs > 0) {
                read_input_line("Trang thai muon chuyen sang khi het gio (ON / OFF): ", act, sizeof(act));
            }
            snprintf(cmd, sizeof(cmd), "SET_TIMER|%s|%d|%s", dev->token, secs, act);
            if (send_device_tcp_command(dev->ip, dev->tcp_port, cmd, resp, sizeof(resp)) == 0) {
                printf("-> Phan hoi tu thiet bi: %s\n", resp);
            }
        } else if (ch == 5) {
            if (dev->type == DEV_FAN) {
                int spd = read_input_int("Nhap toc do gio Quat (1 = Nhe, 2 = Vua, 3 = Manh): ");
                snprintf(cmd, sizeof(cmd), "SET_FAN|%s|%d", dev->token, spd);
                if (send_device_tcp_command(dev->ip, dev->tcp_port, cmd, resp, sizeof(resp)) == 0) {
                    printf("-> Phan hoi tu thiet bi: %s\n", resp);
                }
            } else if (dev->type == DEV_AC) {
                char mode[16];
                read_input_line("Chon che do Dieu hoa (COOL = Lam mat, HEAT = Lam nong, DRY = Hut am): ",
                                mode, sizeof(mode));
                int temp = read_input_int("Nhap nhiet do mong muon (18 .. 30 do C): ");
                snprintf(cmd, sizeof(cmd), "SET_AC|%s|%s|%d", dev->token, mode, temp);
                if (send_device_tcp_command(dev->ip, dev->tcp_port, cmd, resp, sizeof(resp)) == 0) {
                    printf("-> Phan hoi tu thiet bi: %s\n", resp);
                }
            } else {
                printf("[!] Thiet bi nay la Bong den (LIGHT), chi ho tro Bat/Tat va Hen gio.\n");
            }
        } else if (ch == 6) {
            char old_p[PASS_LEN], new_p[PASS_LEN];
            read_input_line("Nhap mat khau hien tai: ", old_p, sizeof(old_p));
            read_input_line("Nhap mat khau moi     : ", new_p, sizeof(new_p));
            snprintf(cmd, sizeof(cmd), "PASSWD|%s|%s|%s", dev->token, old_p, new_p);
            if (send_device_tcp_command(dev->ip, dev->tcp_port, cmd, resp, sizeof(resp)) == 0) {
                printf("-> Phan hoi tu thiet bi: %s\n", resp);
            }
        } else if (ch == 7) {
            authenticate_device(dev);
        }
    }
}

/* ==================== CÁC MENU QUẢN LÝ NHÀ & PHÒNG ==================== */

void menu_manage_houses(void) {
    while (1) {
        house_list(&g_app);
        printf("  1. Them Can nha moi\n");
        printf("  2. Doi ten Can nha\n");
        printf("  3. Xoa Can nha\n");
        printf("  0. Quay lai\n");
        int ch = read_input_int("Chon: ");
        if (ch == 0) break;

        if (ch == 1) {
            char name[NAME_LEN];
            read_input_line("Nhap ten Can nha moi: ", name, sizeof(name));
            if (strlen(name) > 0) {
                int id = house_add(&g_app, name);
                home_save(&g_app, DB_FILE);
                printf("[OK] Da tao Can nha '%s' (ID=%d)\n", name, id);
            }
        } else if (ch == 2) {
            int id = read_input_int("Nhap Ma Nha can doi ten: ");
            char name[NAME_LEN];
            read_input_line("Nhap ten moi: ", name, sizeof(name));
            if (house_rename(&g_app, id, name) == 0) {
                home_save(&g_app, DB_FILE);
                printf("[OK] Da cap nhat ten Can nha.\n");
            } else {
                printf("[LOI] Khong tim thay Ma Nha!\n");
            }
        } else if (ch == 3) {
            int id = read_input_int("Nhap Ma Nha can xoa: ");
            if (house_delete(&g_app, id) == 0) {
                home_save(&g_app, DB_FILE);
                printf("[OK] Da xoa Can nha ID=%d.\n", id);
            } else {
                printf("[LOI] Khong tim thay Ma Nha!\n");
            }
        }
    }
}

void menu_manage_rooms(void) {
    while (1) {
        room_list_all(&g_app);
        printf("  1. Them Phong moi vao Can nha\n");
        printf("  2. Doi ten Phong\n");
        printf("  3. Xoa Phong\n");
        printf("  0. Quay lai\n");
        int ch = read_input_int("Chon: ");
        if (ch == 0) break;

        if (ch == 1) {
            house_list(&g_app);
            int hid = read_input_int("Nhap Ma Nha: ");
            char name[NAME_LEN];
            read_input_line("Nhap ten Phong moi: ", name, sizeof(name));
            int rid = room_add(&g_app, hid, name);
            if (rid > 0) {
                home_save(&g_app, DB_FILE);
                printf("[OK] Da them Phong '%s' (Room ID=%d) vao Nha ID=%d.\n", name, rid, hid);
            } else {
                printf("[LOI] Ma Nha khong hop le!\n");
            }
        } else if (ch == 2) {
            int rid = read_input_int("Nhap Ma Phong can doi ten: ");
            char name[NAME_LEN];
            read_input_line("Nhap ten Phong moi: ", name, sizeof(name));
            if (room_rename(&g_app, rid, name) == 0) {
                home_save(&g_app, DB_FILE);
                printf("[OK] Da doi ten Phong.\n");
            } else {
                printf("[LOI] Khong tim thay Ma Phong!\n");
            }
        } else if (ch == 3) {
            int rid = read_input_int("Nhap Ma Phong can xoa: ");
            if (room_delete(&g_app, rid) == 0) {
                home_save(&g_app, DB_FILE);
                printf("[OK] Da xoa Phong ID=%d.\n", rid);
            } else {
                printf("[LOI] Khong tim thay Ma Phong!\n");
            }
        }
    }
}

void menu_manage_devices(void) {
    while (1) {
        device_list_all(&g_app);
        printf("  1. Xem thiet bi theo Phong\n");
        printf("  2. Them thiet bi thu cong (Nhap IP & Port - danh cho ESP32 / LAN)\n");
        printf("  3. Xoa thiet bi khoi Phong\n");
        printf("  0. Quay lai\n");
        int ch = read_input_int("Chon: ");
        if (ch == 0) break;

        if (ch == 1) {
            room_list_all(&g_app);
            int rid = read_input_int("Nhap Ma Phong: ");
            device_list_by_room(&g_app, rid);
        } else if (ch == 2) {
            room_list_all(&g_app);
            int rid = read_input_int("Nhap Ma Phong muon them thiet bi: ");
            if (!room_find(&g_app, rid)) {
                printf("[LOI] Ma Phong khong ton tai!\n");
                continue;
            }
            char did[ID_LEN], tstr[16], name[NAME_LEN], ip[64];
            read_input_line("Nhap Ma Thiet Bi (VD: ESP32_01): ", did, sizeof(did));
            read_input_line("Nhap Loai Thiet Bi (LIGHT / FAN / AC): ", tstr, sizeof(tstr));
            read_input_line("Nhap Ten Thiet Bi (VD: Den_ESP32): ", name, sizeof(name));
            read_input_line("Nhap Dia chi IP (VD: 192.168.1.50 hoac 127.0.0.1): ", ip, sizeof(ip));
            int port = read_input_int("Nhap Cong TCP (VD: 8001): ");

            if (device_add_to_room(&g_app, rid, did, str_to_device_type(tstr), name, ip, port) == 0) {
                home_save(&g_app, DB_FILE);
                printf("[OK] Da luu thiet bi '%s' vao Phong ID=%d!\n", name, rid);
            }
        } else if (ch == 3) {
            char did[ID_LEN];
            read_input_line("Nhap Ma Thiet Bi (ID) can xoa: ", did, sizeof(did));
            if (device_remove(&g_app, did) == 0) {
                home_save(&g_app, DB_FILE);
                printf("[OK] Da xoa thiet bi '%s'.\n", did);
            } else {
                printf("[LOI] Khong tim thay thiet bi!\n");
            }
        }
    }
}

int main(void) {
    home_init(&g_app, DB_FILE);

    while (1) {
        printf("\n====================================================================\n");
        printf("   HE THONG QUAN LY & DIEU KHIEN THIET BI DIEN THONG MINH (IT4062)  \n");
        printf("   Ma Ung Dung (App ID): %-20s                       \n", g_app.app_id);
        printf("====================================================================\n");
        printf("  1. Quan ly Can nha (Houses)\n");
        printf("  2. Quan ly Phong trong nha (Rooms)\n");
        printf("  3. Quan ly Thiet bi trong phong (Devices)\n");
        printf("  4. Quet tim thiet bi tren mang (UDP Broadcast Scan)\n");
        printf("  5. Ket noi & Dieu khien Thiet bi (Bat/Tat, Hen gio, Quat, Dieu hoa, Dien nang)\n");
        printf("  0. Thoat chuong trinh\n");
        printf("--------------------------------------------------------------------\n");

        int choice = read_input_int("Nhap lua chon cua ban: ");
        if (choice == 0) {
            printf("Da luu du lieu. Tam biet!\n");
            break;
        }

        switch (choice) {
            case 1:
                menu_manage_houses();
                break;
            case 2:
                menu_manage_rooms();
                break;
            case 3:
                menu_manage_devices();
                break;
            case 4:
                menu_scan_devices();
                break;
            case 5: {
                device_list_all(&g_app);
                char did[ID_LEN];
                read_input_line("Nhap Ma Thiet Bi (ID) muon dieu khien (Enter de huy): ", did, sizeof(did));
                if (strlen(did) == 0) break;
                ManagedDevice *dev = device_find(&g_app, did);
                if (!dev) {
                    printf("[LOI] Khong tim thay thiet bi co ID '%s'! Hay Quet (muc 4) hoac Them (muc 3) truoc.\n", did);
                } else {
                    control_device_panel(dev);
                }
                break;
            }
            default:
                printf("[!] Lua chon khong hop le.\n");
                break;
        }
    }

    return 0;
}
