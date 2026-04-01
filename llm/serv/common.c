#include "common.h"
#include <signal.h>

int CONFIG_TIMEOUT = 10;

static int alarm_flag = 0;
static struct sigaction old_sigaction;

void *nt_malloc2(const char *filename, int line, void *old, size_t size)
{
    void *ptr = NULL;
    NT_UNUSED(filename);
    NT_UNUSED(line);
    NT_UNUSED(old);
    
    ptr = malloc(size);
    if (NULL == ptr) {
        fprintf(stderr, "malloc failed: %s\n", strerror(errno));
        exit(1);
    }
    
    return ptr;
}

void *nt_realloc2(const char *filename, int line, void *old, size_t size)
{
    void *ptr = NULL;
    NT_UNUSED(filename);
    NT_UNUSED(line);
    
    ptr = realloc(old, size);
    if (NULL == ptr && 0 != size) {
        fprintf(stderr, "realloc failed: %s\n", strerror(errno));
        exit(1);
    }
    
    return ptr;
}

char *nt_strdup2(const char *filename, int line, char *old, const char *str)
{
    char *result = NULL;
    NT_UNUSED(filename);
    NT_UNUSED(line);
    
    nt_free(old);
    
    result = strdup(str);
    if (NULL == result) {
        fprintf(stderr, "strdup failed: %s\n", strerror(errno));
        exit(1);
    }
    
    return result;
}

size_t nt_strlcpy(char *dst, const char *src, size_t siz)
{
    char *d = dst;
    const char *s = src;
    size_t n = siz;
    
    if (n != 0) {
        while (--n != 0) {
            if ((*d++ = *s++) == '\0')
                break;
        }
    }
    
    if (n == 0) {
        if (siz != 0)
            *d = '\0';
        while (*s++)
            ;
    }
    
    return (s - src - 1);
}

void nt_strlcat(char *dst, const char *src, size_t siz)
{
    char *d = dst;
    const char *s = src;
    size_t n = siz;
    size_t dlen;
    
    while (n-- != 0 && *d != '\0')
        d++;
    dlen = d - dst;
    n = siz - dlen;
    
    if (n == 0)
        return;
    
    while (*s != '\0') {
        if (n != 1) {
            *d++ = *s++;
            n--;
        } else
            break;
    }
    
    *d = '\0';
}

char *nt_dsprintf(char *dest, const char *f, ...)
{
    va_list args;
    char *str = NULL;
    int n;
    
    va_start(args, f);
    n = vsnprintf(NULL, 0, f, args);
    va_end(args);
    
    if (n < 0)
        return dest;
    
    str = nt_malloc(dest, (size_t)(n + 1));
    
    va_start(args, f);
    vsnprintf(str, (size_t)(n + 1), f, args);
    va_end(args);
    
    return str;
}

const char *strerror_from_system(int error)
{
    return strerror(error);
}

size_t nt_vsnprintf(char *str, size_t count, const char *fmt, va_list args)
{
    size_t ret;
    
    ret = (size_t)vsnprintf(str, count, fmt, args);
    
    if (ret >= count) {
        if (count > 0)
            str[count - 1] = '\0';
        ret = count - 1;
    }
    
    return ret;
}

void __nt_nt_snprintf(char *str, size_t count, const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    nt_vsnprintf(str, count, fmt, args);
    va_end(args);
}

void nt_strncpy_alloc(char **str, size_t *alloc_len, size_t *offset, const char *src, size_t n)
{
    if (NULL == *str) {
        *alloc_len = n + 1;
        *str = nt_malloc(NULL, *alloc_len);
        *offset = 0;
    } else if (*offset + n >= *alloc_len) {
        *alloc_len = *offset + n + 1;
        *str = nt_realloc(*str, *alloc_len);
    }
    
    memcpy(*str + *offset, src, n);
    *offset += n;
    (*str)[*offset] = '\0';
}

int is_ip4(const char *ip)
{
    struct in_addr addr;
    return inet_aton(ip, &addr) != 0;
}

int is_ip6(const char *ip)
{
#ifdef HAVE_IPV6
    struct in6_addr addr;
    return inet_pton(AF_INET6, ip, &addr) == 1;
#else
    return FAIL;
#endif
}

int is_supported_ip(const char *ip)
{
    return is_ip4(ip) || is_ip6(ip);
}

int is_ip(const char *ip)
{
    return is_supported_ip(ip);
}

int nt_validate_hostname(const char *hostname)
{
    const char *p = hostname;
    int len = 0;
    
    if (NULL == hostname || '\0' == *hostname)
        return FAIL;
    
    while (*p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '.'))
            return FAIL;
        p++;
        len++;
    }
    
    if (len > MAX_NT_DNSNAME_LEN)
        return FAIL;
    
    return SUCCEED;
}

int is_uint_n_range(const char *str, size_t n, void *value, size_t size, nt_uint64_t min, nt_uint64_t max)
{
    nt_uint64_t num = 0;
    const char *p = str;
    
    NT_UNUSED(n);
    
    if (NULL == str || '\0' == *str)
        return FAIL;
    
    while (*p && *p >= '0' && *p <= '9') {
        num = num * 10 + (*p - '0');
        if (num > max)
            return FAIL;
        p++;
    }
    
    if (*p != '\0')
        return FAIL;
    
    if (num < min)
        return FAIL;
    
    if (value) {
        if (size == sizeof(unsigned int))
            *(unsigned int *)value = (unsigned int)num;
        else if (size == sizeof(nt_uint64_t))
            *(nt_uint64_t *)value = num;
        else if (size == sizeof(unsigned short))
            *(unsigned short *)value = (unsigned short)num;
    }
    
    return SUCCEED;
}

double nt_time(void)
{
    struct timeval tv;
    
    if (0 != gettimeofday(&tv, NULL))
        return 0.0;
    
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static void alarm_handler(int sig)
{
    NT_UNUSED(sig);
    alarm_flag = 1;
}

unsigned int nt_alarm_on(unsigned int seconds)
{
    struct sigaction sa;
    unsigned int ret;
    
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGALRM, &sa, &old_sigaction);
    alarm_flag = 0;
    
    ret = alarm(seconds);
    
    return ret;
}

unsigned int nt_alarm_off(void)
{
    unsigned int ret;
    
    ret = alarm(0);
    sigaction(SIGALRM, &old_sigaction, NULL);
    alarm_flag = 0;
    
    return ret;
}

int nt_alarm_timed_out(void)
{
    return alarm_flag;
}

void nt_alarm_flag_set(void)
{
    alarm_flag = 1;
}

void nt_alarm_flag_clear(void)
{
    alarm_flag = 0;
}

nt_uint64_t nt_letoh_uint64(nt_uint64_t data)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return data;
#else
    return ((data >> 56) & 0xff) |
           ((data >> 40) & 0xff00) |
           ((data >> 24) & 0xff0000) |
           ((data >> 8) & 0xff000000) |
           ((data << 8) & 0xff00000000) |
           ((data << 24) & 0xff0000000000) |
           ((data << 40) & 0xff000000000000) |
           ((data << 56) & 0xff00000000000000);
#endif
}

nt_uint64_t nt_htole_uint64(nt_uint64_t data)
{
    return nt_letoh_uint64(data);
}

void nt_error(const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}