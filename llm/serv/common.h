#ifndef NT_COMMON_H
#define NT_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <stdint.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>
#include <sys/select.h>
#include <sys/stat.h>

#define SUCCEED 0
#define FAIL -1

#define NT_UNUSED(var) (void)(var)

#define MAX_STRING_LEN 2048
#define MAX_NT_DNSNAME_LEN 255

typedef uint64_t nt_uint64_t;
typedef uint32_t nt_uint32_t;
typedef int32_t nt_int32_t;

#define NT_KIBIBYTE 1024
#define NT_MEBIBYTE 1048576
#define NT_GIBIBYTE 1073741824

#if __WORDSIZE == 64
#define NT_FS_UI64 "%lu"
#define NT_FS_I64 "%ld"
#else
#define NT_FS_UI64 "%llu"
#define NT_FS_I64 "%lld"
#endif

#define NT_MAX_RECV_DATA_SIZE (128 * NT_MEBIBYTE)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define ARRSIZE(a) (sizeof(a) / sizeof(*a))

void *nt_malloc2(const char *filename, int line, void *old, size_t size);
void *nt_realloc2(const char *filename, int line, void *old, size_t size);
char *nt_strdup2(const char *filename, int line, char *old, const char *str);

#define nt_malloc(old, size) nt_malloc2(__FILE__, __LINE__, old, size)
#define nt_realloc(src, size) nt_realloc2(__FILE__, __LINE__, src, size)
#define nt_strdup(old, str) nt_strdup2(__FILE__, __LINE__, old, str)

#define nt_free(ptr) \
do { \
    if (ptr) { \
        free(ptr); \
        ptr = NULL; \
    } \
} while (0)

size_t nt_strlcpy(char *dst, const char *src, size_t siz);
void nt_strlcat(char *dst, const char *src, size_t siz);
char *nt_dsprintf(char *dest, const char *f, ...);

#define strscpy(x, y) nt_strlcpy(x, y, sizeof(x))
#define strscat(x, y) nt_strlcat(x, y, sizeof(x))

const char *strerror_from_system(int error);

size_t nt_vsnprintf(char *str, size_t count, const char *fmt, va_list args);
void __nt_nt_snprintf(char *str, size_t count, const char *fmt, ...);
#define nt_snprintf(str, count, fmt, ...) __nt_nt_snprintf(str, count, fmt, ##__VA_ARGS__)

void nt_strncpy_alloc(char **str, size_t *alloc_len, size_t *offset, const char *src, size_t n);

int is_ip4(const char *ip);
int is_ip6(const char *ip);
int is_supported_ip(const char *ip);
int is_ip(const char *ip);

int nt_validate_hostname(const char *hostname);

int is_uint_n_range(const char *str, size_t n, void *value, size_t size, nt_uint64_t min, nt_uint64_t max);
#define is_uint_range(str, value, min, max) \
    is_uint_n_range(str, MAX_STRING_LEN, value, sizeof(unsigned int), min, max)

double nt_time(void);

unsigned int nt_alarm_on(unsigned int seconds);
unsigned int nt_alarm_off(void);
int nt_alarm_timed_out(void);
void nt_alarm_flag_set(void);
void nt_alarm_flag_clear(void);

nt_uint64_t nt_letoh_uint64(nt_uint64_t data);
nt_uint64_t nt_htole_uint64(nt_uint64_t data);

void nt_error(const char *fmt, ...);

extern int CONFIG_TIMEOUT;

#endif