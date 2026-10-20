#ifndef PROTOCOL_H
#define PROTOCOL_H

/* =========================================================================
 * ĐẶC TẢ GIAO THỨC ỨNG DỤNG ĐIỀU KHIỂN THIẾT BỊ ĐIỆN (IT4062)
 * =========================================================================
 * 1. Giao thức Quét thiết bị (UDP Broadcast - Port 9999):
 *    - Client gửi   : SCAN_REQ\r\n
 *    - Thiết bị trả : SCAN_RES|<dev_id>|<type>|<tcp_port>|<dev_name>\r\n
 *
 * 2. Giao thức Điều khiển thiết bị (TCP - Truyền dòng kết thúc bởi \r\n):
 *    - Xác thực     : AUTH|<app_id>|<password>
 *      -> Thành công: 100|AUTH_OK|<token>|<dev_id>|<type>|<dev_name>
 *      -> Thất bại  : 201|WRONG_PASSWORD
 *
 *    - Đổi mật khẩu : PASSWD|<token>|<old_pass>|<new_pass>
 *      -> Thành công: 100|PASSWD_OK
 *      -> Thất bại  : 202|WRONG_OLD_PASSWORD / 203|UNAUTHORIZED
 *
 *    - Bật / Tắt    : SET_POWER|<token>|<ON|OFF>
 *      -> Thành công: 100|POWER_OK|<ON|OFF>|<watt>
 *
 *    - Hẹn giờ      : SET_TIMER|<token>|<seconds>|<ON|OFF>
 *      -> Thành công: 100|TIMER_OK|<seconds>|<ON|OFF>
 *
 *    - Tốc độ quạt  : SET_FAN|<token>|<1|2|3>
 *      -> Thành công: 100|FAN_OK|<speed>|<watt>
 *
 *    - Chế độ ĐH    : SET_AC|<token>|<COOL|HEAT|DRY>|<18..30>
 *      -> Thành công: 100|AC_OK|<mode>|<temp>|<watt>
 *
 *    - Lấy thông tin: GET_INFO|<token>
 *      -> Thành công: 100|INFO|<id>|<type>|<name>|<power>|<watt>|<fan_speed>|
 *                     <ac_mode>|<ac_temp>|<timer_rem>|<timer_act>|
 *                     <day_sec>|<month_sec>|<year_sec>|<total_wh>
 * ========================================================================= */

#define DISCOVERY_UDP_PORT 9999
#define MAX_LINE_LEN       1024
#define TOKEN_LEN          32
#define ID_LEN             32
#define NAME_LEN           64
#define PASS_LEN           64

/* Mã phản hồi chuẩn (Status Codes) */
#define STATUS_OK                100
#define ERR_WRONG_PASSWORD       201
#define ERR_WRONG_OLD_PASSWORD   202
#define ERR_UNAUTHORIZED         203
#define ERR_INVALID_PARAM        204
#define ERR_UNKNOWN_CMD          205

/* Loại thiết bị */
typedef enum {
    DEV_LIGHT = 1,  /* Bóng đèn */
    DEV_FAN   = 2,  /* Quạt điện */
    DEV_AC    = 3   /* Điều hòa */
} DeviceType;

/* Chế độ điều hòa */
typedef enum {
    AC_MODE_COOL = 1, /* Làm mát */
    AC_MODE_HEAT = 2, /* Làm nóng */
    AC_MODE_DRY  = 3  /* Hút ẩm */
} ACMode;

/* Công suất tiêu thụ chuẩn (Watt) */
#define WATT_LIGHT_ON       40.0
#define WATT_FAN_SPEED_1    30.0
#define WATT_FAN_SPEED_2    50.0
#define WATT_FAN_SPEED_3    75.0
#define WATT_AC_DRY        600.0
#define WATT_AC_BASE      1000.0
#define WATT_AC_PER_DEG     50.0

#endif /* PROTOCOL_H */
