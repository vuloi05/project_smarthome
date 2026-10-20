#include "home_manager.h"
#include "../common/net_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void home_init(UserAppData *data, const char *db_file) {
    memset(data, 0, sizeof(UserAppData));
    data->next_house_id = 1;
    data->next_room_id  = 1;

    if (home_load(data, db_file) == 0 && strlen(data->app_id) > 0) {
        return;
    }

    /* Nếu chạy lần đầu tiên: Khởi tạo App ID ngẫu nhiên duy nhất cho ứng dụng */
    char hex[12];
    generate_random_hex(hex, 8);
    snprintf(data->app_id, sizeof(data->app_id), "APP_%s", hex);

    /* Tạo sẵn 1 căn nhà và 2 phòng mẫu để thuận tiện sử dụng ngay */
    int h_id = house_add(data, "Nha_Chinh");
    room_add(data, h_id, "Phong_Khach");
    room_add(data, h_id, "Phong_Ngu");

    home_save(data, db_file);
}

int home_save(const UserAppData *data, const char *db_file) {
    if (db_file == NULL) return -1;
    FILE *fp = fopen(db_file, "w");
    if (!fp) return -1;

    fprintf(fp, "APP_ID|%s|%d|%d\n", data->app_id, data->next_house_id, data->next_room_id);

    for (int i = 0; i < MAX_HOUSES; i++) {
        if (data->houses[i].used) {
            fprintf(fp, "HOUSE|%d|%s\n", data->houses[i].house_id, data->houses[i].name);
        }
    }

    for (int i = 0; i < MAX_ROOMS; i++) {
        if (data->rooms[i].used) {
            fprintf(fp, "ROOM|%d|%d|%s\n",
                    data->rooms[i].room_id, data->rooms[i].house_id, data->rooms[i].name);
        }
    }

    for (int i = 0; i < MAX_USER_DEVICES; i++) {
        if (data->devices[i].used) {
            fprintf(fp, "DEVICE|%s|%d|%s|%s|%s|%d|%s\n",
                    data->devices[i].dev_id,
                    data->devices[i].room_id,
                    device_type_to_str(data->devices[i].type),
                    data->devices[i].name,
                    data->devices[i].ip,
                    data->devices[i].tcp_port,
                    strlen(data->devices[i].token) > 0 ? data->devices[i].token : "NONE");
        }
    }

    fclose(fp);
    return 0;
}

int home_load(UserAppData *data, const char *db_file) {
    if (db_file == NULL) return -1;
    FILE *fp = fopen(db_file, "r");
    if (!fp) return -1;

    char line[512];
    int h_idx = 0, r_idx = 0, d_idx = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;

        char *saveptr = NULL;
        char *tag = strtok_r(line, "|", &saveptr);
        if (!tag) continue;

        if (strcmp(tag, "APP_ID") == 0) {
            char *aid = strtok_r(NULL, "|", &saveptr);
            char *nh  = strtok_r(NULL, "|", &saveptr);
            char *nr  = strtok_r(NULL, "|", &saveptr);
            if (aid) strncpy(data->app_id, aid, sizeof(data->app_id) - 1);
            if (nh)  data->next_house_id = atoi(nh);
            if (nr)  data->next_room_id  = atoi(nr);
        } else if (strcmp(tag, "HOUSE") == 0 && h_idx < MAX_HOUSES) {
            char *hid  = strtok_r(NULL, "|", &saveptr);
            char *name = strtok_r(NULL, "|", &saveptr);
            if (hid && name) {
                data->houses[h_idx].used     = 1;
                data->houses[h_idx].house_id = atoi(hid);
                strncpy(data->houses[h_idx].name, name, NAME_LEN - 1);
                h_idx++;
            }
        } else if (strcmp(tag, "ROOM") == 0 && r_idx < MAX_ROOMS) {
            char *rid  = strtok_r(NULL, "|", &saveptr);
            char *hid  = strtok_r(NULL, "|", &saveptr);
            char *name = strtok_r(NULL, "|", &saveptr);
            if (rid && hid && name) {
                data->rooms[r_idx].used     = 1;
                data->rooms[r_idx].room_id  = atoi(rid);
                data->rooms[r_idx].house_id = atoi(hid);
                strncpy(data->rooms[r_idx].name, name, NAME_LEN - 1);
                r_idx++;
            }
        } else if (strcmp(tag, "DEVICE") == 0 && d_idx < MAX_USER_DEVICES) {
            char *did  = strtok_r(NULL, "|", &saveptr);
            char *rid  = strtok_r(NULL, "|", &saveptr);
            char *tstr = strtok_r(NULL, "|", &saveptr);
            char *name = strtok_r(NULL, "|", &saveptr);
            char *ip   = strtok_r(NULL, "|", &saveptr);
            char *port = strtok_r(NULL, "|", &saveptr);
            char *tok  = strtok_r(NULL, "|", &saveptr);

            if (did && rid && tstr && name && ip && port) {
                data->devices[d_idx].used     = 1;
                strncpy(data->devices[d_idx].dev_id, did, ID_LEN - 1);
                data->devices[d_idx].room_id  = atoi(rid);
                data->devices[d_idx].type     = str_to_device_type(tstr);
                strncpy(data->devices[d_idx].name, name, NAME_LEN - 1);
                strncpy(data->devices[d_idx].ip, ip, sizeof(data->devices[d_idx].ip) - 1);
                data->devices[d_idx].tcp_port = atoi(port);
                if (tok && strcmp(tok, "NONE") != 0) {
                    strncpy(data->devices[d_idx].token, tok, TOKEN_LEN - 1);
                } else {
                    data->devices[d_idx].token[0] = '\0';
                }
                d_idx++;
            }
        }
    }

    fclose(fp);
    return 0;
}

/* ==================== QUẢN LÝ CĂN NHÀ ==================== */

int house_add(UserAppData *data, const char *name) {
    for (int i = 0; i < MAX_HOUSES; i++) {
        if (!data->houses[i].used) {
            data->houses[i].used     = 1;
            data->houses[i].house_id = data->next_house_id++;
            strncpy(data->houses[i].name, name, NAME_LEN - 1);
            return data->houses[i].house_id;
        }
    }
    return -1;
}

House* house_find(UserAppData *data, int house_id) {
    for (int i = 0; i < MAX_HOUSES; i++) {
        if (data->houses[i].used && data->houses[i].house_id == house_id) {
            return &data->houses[i];
        }
    }
    return NULL;
}

int house_rename(UserAppData *data, int house_id, const char *new_name) {
    House *h = house_find(data, house_id);
    if (!h) return -1;
    strncpy(h->name, new_name, NAME_LEN - 1);
    return 0;
}

int house_delete(UserAppData *data, int house_id) {
    House *h = house_find(data, house_id);
    if (!h) return -1;

    /* Xóa tất cả các phòng và thiết bị thuộc căn nhà này */
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (data->rooms[i].used && data->rooms[i].house_id == house_id) {
            room_delete(data, data->rooms[i].room_id);
        }
    }
    h->used = 0;
    return 0;
}

void house_list(const UserAppData *data) {
    printf("\n+----------+----------------------------------+\n");
    printf("| Ma Nha   | Ten Can Nha                      |\n");
    printf("+----------+----------------------------------+\n");
    int count = 0;
    for (int i = 0; i < MAX_HOUSES; i++) {
        if (data->houses[i].used) {
            printf("| %-8d | %-32s |\n", data->houses[i].house_id, data->houses[i].name);
            count++;
        }
    }
    if (count == 0) {
        printf("|          (Chua co can nha nao)              |\n");
    }
    printf("+----------+----------------------------------+\n");
}

/* ==================== QUẢN LÝ PHÒNG ==================== */

int room_add(UserAppData *data, int house_id, const char *name) {
    if (house_find(data, house_id) == NULL) return -1;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!data->rooms[i].used) {
            data->rooms[i].used     = 1;
            data->rooms[i].room_id  = data->next_room_id++;
            data->rooms[i].house_id = house_id;
            strncpy(data->rooms[i].name, name, NAME_LEN - 1);
            return data->rooms[i].room_id;
        }
    }
    return -1;
}

Room* room_find(UserAppData *data, int room_id) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (data->rooms[i].used && data->rooms[i].room_id == room_id) {
            return &data->rooms[i];
        }
    }
    return NULL;
}

int room_rename(UserAppData *data, int room_id, const char *new_name) {
    Room *r = room_find(data, room_id);
    if (!r) return -1;
    strncpy(r->name, new_name, NAME_LEN - 1);
    return 0;
}

int room_delete(UserAppData *data, int room_id) {
    Room *r = room_find(data, room_id);
    if (!r) return -1;

    /* Xóa các thiết bị thuộc phòng này */
    for (int i = 0; i < MAX_USER_DEVICES; i++) {
        if (data->devices[i].used && data->devices[i].room_id == room_id) {
            data->devices[i].used = 0;
        }
    }
    r->used = 0;
    return 0;
}

void room_list_by_house(const UserAppData *data, int house_id) {
    printf("\n+----------+----------------------------------+\n");
    printf("| Ma Phong | Ten Phong (Nha ID=%-2d)            |\n", house_id);
    printf("+----------+----------------------------------+\n");
    int count = 0;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (data->rooms[i].used && data->rooms[i].house_id == house_id) {
            printf("| %-8d | %-32s |\n", data->rooms[i].room_id, data->rooms[i].name);
            count++;
        }
    }
    if (count == 0) {
        printf("|          (Chua co phong nao)                |\n");
    }
    printf("+----------+----------------------------------+\n");
}

void room_list_all(const UserAppData *data) {
    printf("\n+----------+----------+------------------------+------------------------+\n");
    printf("| Ma Phong | Ma Nha   | Ten Can Nha            | Ten Phong              |\n");
    printf("+----------+----------+------------------------+------------------------+\n");
    int count = 0;
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (data->rooms[i].used) {
            const char *h_name = "Unknown";
            for (int j = 0; j < MAX_HOUSES; j++) {
                if (data->houses[j].used && data->houses[j].house_id == data->rooms[i].house_id) {
                    h_name = data->houses[j].name;
                    break;
                }
            }
            printf("| %-8d | %-8d | %-22s | %-22s |\n",
                   data->rooms[i].room_id, data->rooms[i].house_id, h_name, data->rooms[i].name);
            count++;
        }
    }
    if (count == 0) {
        printf("|                      (Chua co phong nao)                              |\n");
    }
    printf("+----------+----------+------------------------+------------------------+\n");
}

/* ==================== QUẢN LÝ THIẾT BỊ TRONG PHÒNG ==================== */

ManagedDevice* device_find(UserAppData *data, const char *dev_id) {
    for (int i = 0; i < MAX_USER_DEVICES; i++) {
        if (data->devices[i].used && strcasecmp(data->devices[i].dev_id, dev_id) == 0) {
            return &data->devices[i];
        }
    }
    return NULL;
}

int device_add_to_room(UserAppData *data, int room_id, const char *dev_id,
                       DeviceType type, const char *name, const char *ip, int tcp_port) {
    if (room_find(data, room_id) == NULL) return -1;

    /* Nếu thiết bị đã tồn tại thì cập nhật lại phòng và địa chỉ IP:Port */
    ManagedDevice *existing = device_find(data, dev_id);
    if (existing != NULL) {
        existing->room_id  = room_id;
        existing->type     = type;
        strncpy(existing->name, name, NAME_LEN - 1);
        strncpy(existing->ip, ip, sizeof(existing->ip) - 1);
        existing->tcp_port = tcp_port;
        return 0;
    }

    for (int i = 0; i < MAX_USER_DEVICES; i++) {
        if (!data->devices[i].used) {
            data->devices[i].used     = 1;
            strncpy(data->devices[i].dev_id, dev_id, ID_LEN - 1);
            data->devices[i].room_id  = room_id;
            data->devices[i].type     = type;
            strncpy(data->devices[i].name, name, NAME_LEN - 1);
            strncpy(data->devices[i].ip, ip, sizeof(data->devices[i].ip) - 1);
            data->devices[i].tcp_port = tcp_port;
            data->devices[i].token[0] = '\0';
            return 0;
        }
    }
    return -1;
}

int device_remove(UserAppData *data, const char *dev_id) {
    ManagedDevice *d = device_find(data, dev_id);
    if (!d) return -1;
    d->used = 0;
    return 0;
}

void device_list_by_room(const UserAppData *data, int room_id) {
    printf("\n+------------+--------+----------------------+-----------------+-------+-------------+\n");
    printf("| Ma TB (ID) | Loai   | Ten Thiet Bi         | Dia chi IP      | Port  | Trang Thai  |\n");
    printf("+------------+--------+----------------------+-----------------+-------+-------------+\n");
    int count = 0;
    for (int i = 0; i < MAX_USER_DEVICES; i++) {
        if (data->devices[i].used && data->devices[i].room_id == room_id) {
            printf("| %-10s | %-6s | %-20s | %-15s | %-5d | %-11s |\n",
                   data->devices[i].dev_id,
                   device_type_to_str(data->devices[i].type),
                   data->devices[i].name,
                   data->devices[i].ip,
                   data->devices[i].tcp_port,
                   strlen(data->devices[i].token) > 0 ? "Da xac thuc" : "Chua ket noi");
            count++;
        }
    }
    if (count == 0) {
        printf("|                        (Chua co thiet bi nao trong phong)                          |\n");
    }
    printf("+------------+--------+----------------------+-----------------+-------+-------------+\n");
}

void device_list_all(const UserAppData *data) {
    printf("\n+------------+--------+----------------------+----------+-----------------+-------+-------------+\n");
    printf("| Ma TB (ID) | Loai   | Ten Thiet Bi         | Ma Phong | Dia chi IP      | Port  | Xac thuc    |\n");
    printf("+------------+--------+----------------------+----------+-----------------+-------+-------------+\n");
    int count = 0;
    for (int i = 0; i < MAX_USER_DEVICES; i++) {
        if (data->devices[i].used) {
            printf("| %-10s | %-6s | %-20s | %-8d | %-15s | %-5d | %-11s |\n",
                   data->devices[i].dev_id,
                   device_type_to_str(data->devices[i].type),
                   data->devices[i].name,
                   data->devices[i].room_id,
                   data->devices[i].ip,
                   data->devices[i].tcp_port,
                   strlen(data->devices[i].token) > 0 ? "Co Token" : "Chua co");
            count++;
        }
    }
    if (count == 0) {
        printf("|                             (Chua co thiet bi nao duoc luu)                                   |\n");
    }
    printf("+------------+--------+----------------------+----------+-----------------+-------+-------------+\n");
}
