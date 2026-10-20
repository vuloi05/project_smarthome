#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <stddef.h>
#include "protocol.h"

/**
 * @brief Nhận một dòng hoàn chỉnh kết thúc bởi '\n' từ TCP socket (Xử lý truyền dòng)
 *        Loại bỏ ký tự '\r' và '\n' ở cuối chuỗi.
 * @param sockfd Mô tả socket
 * @param buf Bộ đệm lưu chuỗi kết quả
 * @param max_len Kích thước tối đa của bộ đệm
 * @return Số byte của dòng (>= 0), 0 nếu kết nối đóng và chưa đọc được gì, -1 nếu lỗi
 */
int recv_line(int sockfd, char *buf, size_t max_len);

/**
 * @brief Gửi một dòng dữ liệu qua TCP socket, tự động đính kèm "\r\n" nếu chưa có
 * @param sockfd Mô tả socket
 * @param str Chuỗi cần gửi
 * @return 0 nếu thành công, -1 nếu lỗi
 */
int send_line(int sockfd, const char *str);

/**
 * @brief Sinh chuỗi mã ngẫu nhiên dạng Hex (dùng tạo App ID và Token)
 * @param out Bộ đệm nhận chuỗi (cần ít nhất len + 1 byte)
 * @param len Độ dài chuỗi mong muốn
 */
void generate_random_hex(char *out, size_t len);

/**
 * @brief Chuyển đổi giữa kiểu liệt kê DeviceType và chuỗi ký tự
 */
const char* device_type_to_str(DeviceType type);
DeviceType str_to_device_type(const char *str);

/**
 * @brief Chuyển đổi giữa kiểu liệt kê ACMode và chuỗi ký tự
 */
const char* ac_mode_to_str(ACMode mode);
ACMode str_to_ac_mode(const char *str);

/**
 * @brief Tính toán công suất tiêu thụ điện hiện tại (Watt) dựa theo trạng thái và mức điều chỉnh
 */
double calculate_watt(DeviceType type, int power_on, int fan_speed, ACMode ac_mode, int ac_temp);

/**
 * @brief Ghi nhật ký hoạt động kèm nhãn thời gian [YYYY-MM-DD HH:MM:SS] vào file log
 */
void write_log(const char *log_file, const char *fmt, ...);

#endif /* NET_UTILS_H */
