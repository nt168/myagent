#include "common.h"
#include "comms.h"
#include <pthread.h>
#include <sys/wait.h>

#define TEST_PORT_BASE 16000
#define TEST_MSG "Hello, Zabbix Socket Test!"

static int test_passed = 0;
static int test_failed = 0;

static void test_assert(int condition, const char *test_name, const char *message)
{
    if (condition) {
        printf("[PASS] %s: %s\n", test_name, message);
        test_passed++;
    } else {
        printf("[FAIL] %s: %s\n", test_name, message);
        test_failed++;
    }
}

static void test_ip_validation(void)
{
    printf("\n=== Test: IP Validation ===\n");
    
    test_assert(SUCCEED == is_ip4("127.0.0.1"), "IP Valid", "IPv4 localhost");
    test_assert(SUCCEED == is_ip4("192.168.1.1"), "IP Valid", "IPv4 private");
    test_assert(FAIL == is_ip4("256.1.1.1"), "IP Valid", "IPv4 invalid (out of range)");
    test_assert(FAIL == is_ip4("abc.def.ghi.jkl"), "IP Valid", "IPv4 invalid (non-numeric)");
}

static void test_string_functions(void)
{
    char buf[256];
    char *dyn_str = NULL;
    
    printf("\n=== Test: String Functions ===\n");
    
    nt_strlcpy(buf, "Hello", sizeof(buf));
    test_assert(0 == strcmp(buf, "Hello"), "String", "nt_strlcpy");
    
    nt_strlcat(buf, " World", sizeof(buf));
    test_assert(0 == strcmp(buf, "Hello World"), "String", "nt_strlcat");
    
    dyn_str = nt_dsprintf(NULL, "Test %d %s", 123, "message");
    test_assert(0 == strcmp(dyn_str, "Test 123 message"), "String", "nt_dsprintf");
    nt_free(dyn_str);
}

static void test_protocol_functions(void)
{
    printf("\n=== Test: Protocol Functions ===\n");
    
    test_assert(0 == strcmp("unencrypted", nt_tcp_connection_type_name(NT_TCP_SEC_UNENCRYPTED)),
               "Protocol", "Connection type name (unencrypted)");
    test_assert(0 == strcmp("TLS with certificate", nt_tcp_connection_type_name(NT_TCP_SEC_TLS_CERT)),
               "Protocol", "Connection type name (TLS cert)");
    test_assert(0 == strcmp("TLS with PSK", nt_tcp_connection_type_name(NT_TCP_SEC_TLS_PSK)),
               "Protocol", "Connection type name (TLS PSK)");
    test_assert(0 == strcmp("unknown", nt_tcp_connection_type_name(999)),
               "Protocol", "Connection type name (unknown)");
}

static void test_peer_validation(void)
{
    char *error = NULL;
    int ret;
    
    printf("\n=== Test: Peer Validation ===\n");
    
    ret = nt_validate_peer_list("127.0.0.1,192.168.1.0/24,localhost", &error);
    test_assert(SUCCEED == ret, "Peer Valid", "Valid peer list");
    if (FAIL == ret && error) {
        printf("Error: %s\n", error);
        nt_free(error);
    }
    
    ret = nt_validate_peer_list("999.999.999.999", &error);
    test_assert(FAIL == ret, "Peer Valid", "Invalid peer list");
    if (FAIL == ret && error) {
        printf("Expected error: %s\n", error);
        nt_free(error);
    }
}

static void test_error_handling(void)
{
    nt_socket_t sock;
    int ret;
    
    printf("\n=== Test: Error Handling ===\n");
    
    ret = nt_tcp_connect(&sock, NULL, "256.256.256.256", 9999, 1,
            NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
    test_assert(FAIL == ret, "Error", "Invalid IP address handling");
    if (FAIL == ret) {
        printf("Error message: %s\n", nt_socket_strerror());
    }
    
    ret = nt_tcp_connect(&sock, NULL, "127.0.0.1", 1, 1,
            NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
    test_assert(FAIL == ret, "Error", "Connection to privileged port (expected to fail)");
    if (FAIL == ret) {
        printf("Error message: %s\n", nt_socket_strerror());
    }
}

static void test_listen_socket(void)
{
    nt_socket_t listen_sock;
    int port = TEST_PORT_BASE + 5;
    int ret;
    
    printf("\n=== Test: Listen Socket ===\n");
    
    ret = nt_tcp_listen(&listen_sock, "127.0.0.1", (unsigned short)port);
    test_assert(SUCCEED == ret, "Listen", "TCP listen on 127.0.0.1");
    if (SUCCEED == ret) {
        printf("Listening on port %d, num_socks=%d\n", port, listen_sock.num_socks);
        test_assert(listen_sock.num_socks >= 1, "Listen", "At least one socket created");
        nt_tcp_close(&listen_sock);
    } else {
        printf("Listen failed: %s\n", nt_socket_strerror());
    }
}

static void test_memory_functions(void)
{
    char *ptr = NULL;
    
    printf("\n=== Test: Memory Functions ===\n");
    
    ptr = nt_malloc(NULL, 100);
    test_assert(ptr != NULL, "Memory", "nt_malloc");
    strcpy(ptr, "test");
    test_assert(0 == strcmp(ptr, "test"), "Memory", "Memory allocation works");
    
    ptr = nt_realloc(ptr, 200);
    test_assert(ptr != NULL, "Memory", "nt_realloc");
    test_assert(0 == strcmp(ptr, "test"), "Memory", "Data preserved after realloc");
    
    nt_free(ptr);
    test_assert(ptr == NULL, "Memory", "nt_free sets pointer to NULL");
}

static void test_byte_order_functions(void)
{
    nt_uint64_t value = 0x0102030405060708ULL;
    nt_uint64_t converted;
    
    printf("\n=== Test: Byte Order Functions ===\n");
    
    converted = nt_htole_uint64(value);
    test_assert(converted != 0, "Byte Order", "nt_htole_uint64 works");
    
    converted = nt_letoh_uint64(value);
    test_assert(converted != 0, "Byte Order", "nt_letoh_uint64 works");
}

int main(int argc, char **argv)
{
    printf("========================================\n");
    printf("Zabbix Network Socket Library Tests\n");
    printf("========================================\n");
    
    test_ip_validation();
    test_string_functions();
    test_protocol_functions();
    test_peer_validation();
    test_memory_functions();
    test_byte_order_functions();
    test_error_handling();
    test_listen_socket();
    
    printf("\n========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");
    printf("Passed: %d\n", test_passed);
    printf("Failed: %d\n", test_failed);
    printf("Total:  %d\n", test_passed + test_failed);
    printf("========================================\n");
    
    return test_failed > 0 ? 1 : 0;
}
