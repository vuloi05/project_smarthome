#ifndef HOME_MANAGER_H
#define HOME_MANAGER_H

#include "../common/protocol.h"

#define MAX_HOUSES        16
#define MAX_ROOMS         64
#define MAX_USER_DEVICES  128
#define MAX_SCANNED_DEVS  32

/* Thông tin một Căn nhà */
typedef struct {
    int  used;
    int  house_id;
    char name[NAME_LEN];
} House;

/* Thông tin một Phòng trong Căn nhà */
typedef struct {
    int  used;
    int  room_id;
    int  house_id;
    char name[NAME_LEN];
} Room;

/* Thông tin một Thiết bị được quản lý trong Phòng */
typedef struct {
    int        used;
    char       dev_id[ID_LEN];
    int        room_id;
    DeviceType type;
    char       name[NAME_LEN];
    char       ip[64];
    int        tcp_port;
    char       token[TOKEN_LEN]; /* Token được cấp sau khi xác thực thành công */
} ManagedDevice;

/* Kết quả thiết bị tìm thấy khi Quét (UDP Scan) */
typedef struct {
    char       dev_id[ID_LEN];
    DeviceType type;
    char       name[NAME_LEN];
    char       ip[64];
    int        tcp_port;
} ScannedDevice;

/* Cấu trúc dữ liệu tổng của ứng dụng người dùng */
typedef struct {
    char          app_id[ID_LEN];
    int           next_house_id;
    int           next_room_id;
    House         houses[MAX_HOUSES];
    Room          rooms[MAX_ROOMS];
    ManagedDevice devices[MAX_USER_DEVICES];
} UserAppData;

/* Khởi tạo và lưu/nạp dữ liệu từ file */
void home_init(UserAppData *data, const char *db_file);
int  home_save(const UserAppData *data, const char *db_file);
int  home_load(UserAppData *data, const char *db_file);

/* Quản lý Căn nhà (Tiêu chí 15: 1 điểm) */
int   house_add(UserAppData *data, const char *name);
int   house_rename(UserAppData *data, int house_id, const char *new_name);
int   house_delete(UserAppData *data, int house_id);
House* house_find(UserAppData *data, int house_id);
void  house_list(const UserAppData *data);

/* Quản lý Phòng trong Căn nhà (Tiêu chí 16: 1 điểm) */
int   room_add(UserAppData *data, int house_id, const char *name);
int   room_rename(UserAppData *data, int room_id, const char *new_name);
int   room_delete(UserAppData *data, int room_id);
Room* room_find(UserAppData *data, int room_id);
void  room_list_by_house(const UserAppData *data, int house_id);
void  room_list_all(const UserAppData *data);

/* Quản lý Thiết bị trong Phòng (Tiêu chí 17: 1 điểm) */
int            device_add_to_room(UserAppData *data, int room_id, const char *dev_id,
                                  DeviceType type, const char *name, const char *ip, int tcp_port);
int            device_remove(UserAppData *data, const char *dev_id);
ManagedDevice* device_find(UserAppData *data, const char *dev_id);
void           device_list_by_room(const UserAppData *data, int room_id);
void           device_list_all(const UserAppData *data);

#endif /* HOME_MANAGER_H */
