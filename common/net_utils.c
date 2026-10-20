#include "net_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>

int recv_line(int sockfd, char *buf, size_t max_len) {
    if (max_len == 0) return -1;

    size_t idx = 0;
    char ch;

    while (idx < max_len - 1) {
        ssize_t n = recv(sockfd, &ch, 1, 0);
        if (n == 1) {
            if (ch == '\n') {
                break;
            }
            if (ch != '\r') {
                buf[idx++] = ch;
            }
        } else if (n == 0) {
            /* Phía bên kia đã đóng kết nối */
            if (idx == 0) return 0;
            break;
        } else {
            if (errno == EINTR) continue;
            return -1;
        }
    }

    buf[idx] = '\0';
    return (int)idx;
}

int send_line(int sockfd, const char *str) {
    char temp[MAX_LINE_LEN + 4];
    size_t len = strlen(str);

    if (len >= 2 && str[len - 2] == '\r' && str[len - 1] == '\n') {
        snprintf(temp, sizeof(temp), "%s", str);
    } else if (len >= 1 && str[len - 1] == '\n') {
        snprintf(temp, sizeof(temp), "%.*s\r\n", (int)(len - 1), str);
    } else {
        snprintf(temp, sizeof(temp), "%s\r\n", str);
    }

    size_t total = strlen(temp);
    size_t sent = 0;

    while (sent < total) {
        ssize_t n = send(sockfd, temp + sent, total - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        sent += (size_t)n;
    }

    return 0;
}

void generate_random_hex(char *out, size_t len) {
    static const char hex_chars[] = "0123456789ABCDEF";
    static int seeded = 0;

    int urandom_fd = open("/dev/urandom", O_RDONLY);
    if (urandom_fd >= 0) {
        unsigned char rnd_bytes[64];
        size_t to_read = (len < sizeof(rnd_bytes)) ? len : sizeof(rnd_bytes);
        ssize_t r = read(urandom_fd, rnd_bytes, to_read);
        close(urandom_fd);
        if (r == (ssize_t)to_read) {
            for (size_t i = 0; i < len; i++) {
                out[i] = hex_chars[rnd_bytes[i % to_read] % 16];
            }
            out[len] = '\0';
            return;
        }
    }

    if (!seeded) {
        srand((unsigned int)(time(NULL) ^ getpid()));
        seeded = 1;
    }
    for (size_t i = 0; i < len; i++) {
        out[i] = hex_chars[rand() % 16];
    }
    out[len] = '\0';
}

const char* device_type_to_str(DeviceType type) {
    switch (type) {
        case DEV_LIGHT: return "LIGHT";
        case DEV_FAN:   return "FAN";
        case DEV_AC:    return "AC";
        default:        return "UNKNOWN";
    }
}

DeviceType str_to_device_type(const char *str) {
    if (strcasecmp(str, "LIGHT") == 0) return DEV_LIGHT;
    if (strcasecmp(str, "FAN") == 0)   return DEV_FAN;
    if (strcasecmp(str, "AC") == 0)    return DEV_AC;
    return DEV_LIGHT;
}

const char* ac_mode_to_str(ACMode mode) {
    switch (mode) {
        case AC_MODE_COOL: return "COOL";
        case AC_MODE_HEAT: return "HEAT";
        case AC_MODE_DRY:  return "DRY";
        default:           return "COOL";
    }
}

ACMode str_to_ac_mode(const char *str) {
    if (strcasecmp(str, "COOL") == 0) return AC_MODE_COOL;
    if (strcasecmp(str, "HEAT") == 0) return AC_MODE_HEAT;
    if (strcasecmp(str, "DRY") == 0)  return AC_MODE_DRY;
    return AC_MODE_COOL;
}

double calculate_watt(DeviceType type, int power_on, int fan_speed, ACMode ac_mode, int ac_temp) {
    if (!power_on) return 0.0;

    switch (type) {
        case DEV_LIGHT:
            return WATT_LIGHT_ON;

        case DEV_FAN:
            if (fan_speed == 1) return WATT_FAN_SPEED_1;
            if (fan_speed == 2) return WATT_FAN_SPEED_2;
            if (fan_speed == 3) return WATT_FAN_SPEED_3;
            return WATT_FAN_SPEED_1;

        case DEV_AC:
            if (ac_mode == AC_MODE_DRY) {
                return WATT_AC_DRY;
            } else if (ac_mode == AC_MODE_COOL) {
                /* Nhiệt độ cài đặt càng thấp thì công suất làm lạnh càng lớn */
                int diff = 30 - ac_temp;
                if (diff < 0) diff = 0;
                return WATT_AC_BASE + diff * WATT_AC_PER_DEG;
            } else if (ac_mode == AC_MODE_HEAT) {
                /* Nhiệt độ cài đặt càng cao thì công suất sưởi ấm càng lớn */
                int diff = ac_temp - 18;
                if (diff < 0) diff = 0;
                return WATT_AC_BASE + diff * WATT_AC_PER_DEG;
            }
            return WATT_AC_BASE;

        default:
            return 0.0;
    }
}

void write_log(const char *log_file, const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    char msg_buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    printf("[%s] %s\n", time_buf, msg_buf);
    fflush(stdout);

    if (log_file != NULL && strlen(log_file) > 0) {
        FILE *fp = fopen(log_file, "a");
        if (fp != NULL) {
            fprintf(fp, "[%s] %s\n", time_buf, msg_buf);
            fclose(fp);
        }
    }
}
