#include "common.h"
#include "comms.h"
#include "file_transfer.h"
#include <pthread.h>
#include <sys/stat.h>

#define TEST_PORT 17000
#define TEST_FILE_SIZE (100 * 1024)
#define TEST_LARGE_FILE_SIZE (1024 * 1024)
#define TEST_SAVE_DIR "/tmp/nt_file_test"

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

static int create_test_file(const char *filepath, size_t size)
{
    FILE *fp = fopen(filepath, "wb");
    if (NULL == fp) {
        return FAIL;
    }
    
    char buffer[1024];
    memset(buffer, 'A', sizeof(buffer));
    
    size_t written = 0;
    while (written < size) {
        size_t to_write = (size - written > sizeof(buffer)) ? sizeof(buffer) : size - written;
        if (fwrite(buffer, 1, to_write, fp) != to_write) {
            fclose(fp);
            return FAIL;
        }
        written += to_write;
    }
    
    fclose(fp);
    return SUCCEED;
}

static int compare_files(const char *file1, const char *file2)
{
    FILE *fp1 = fopen(file1, "rb");
    FILE *fp2 = fopen(file2, "rb");
    
    if (NULL == fp1 || NULL == fp2) {
        if (fp1) fclose(fp1);
        if (fp2) fclose(fp2);
        return FAIL;
    }
    
    char buf1[1024], buf2[1024];
    size_t n1, n2;
    int result = SUCCEED;
    
    while (1) {
        n1 = fread(buf1, 1, sizeof(buf1), fp1);
        n2 = fread(buf2, 1, sizeof(buf2), fp2);
        
        if (n1 != n2 || memcmp(buf1, buf2, n1) != 0) {
            result = FAIL;
            break;
        }
        
        if (0 == n1) break;
    }
    
    fclose(fp1);
    fclose(fp2);
    return result;
}

static void *file_server_thread(void *arg)
{
    nt_socket_t listen_sock, client_sock;
    int ret;
    const char *save_dir = (const char *)arg;
    
    ret = nt_tcp_listen(&listen_sock, "127.0.0.1", TEST_PORT);
    if (SUCCEED != ret) {
        printf("Server: listen failed: %s\n", nt_socket_strerror());
        return NULL;
    }
    
    printf("Server: waiting for connection on port %d\n", TEST_PORT);
    
    ret = nt_tcp_accept(&client_sock, NT_TCP_SEC_UNENCRYPTED);
    if (SUCCEED != ret) {
        printf("Server: accept failed: %s\n", nt_socket_strerror());
        nt_tcp_close(&listen_sock);
        return NULL;
    }
    
    printf("Server: accepted connection from %s\n", client_sock.peer);
    
    ret = file_recv(&client_sock, save_dir, 30);
    if (FILE_TRANS_OK == ret) {
        printf("Server: file received successfully\n");
    } else {
        printf("Server: file receive failed: %d\n", ret);
    }
    
    nt_tcp_close(&client_sock);
    nt_tcp_close(&listen_sock);
    
    return NULL;
}

static void test_small_file_transfer(void)
{
    nt_socket_t client_sock;
    pthread_t server_tid;
    int ret;
    char test_file[] = "/tmp/test_small.bin";
    char error_msg[MAX_STRING_LEN];
    struct stat st;
    
    printf("\n=== Test: Small File Transfer ===\n");
    
    ret = mkdir(TEST_SAVE_DIR, 0755);
    if (0 != ret && EEXIST != errno) {
        printf("Cannot create test directory: %s\n", strerror(errno));
        return;
    }
    
    ret = create_test_file(test_file, TEST_FILE_SIZE);
    test_assert(SUCCEED == ret, "Small File", "Create test file");
    if (SUCCEED != ret) {
        return;
    }
    
    stat(test_file, &st);
    printf("Created test file: %s, size: %ld bytes\n", test_file, (long)st.st_size);
    
    pthread_create(&server_tid, NULL, file_server_thread, TEST_SAVE_DIR);
    usleep(200000);
    
    ret = nt_tcp_connect(&client_sock, NULL, "127.0.0.1", TEST_PORT, 10,
            NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
    test_assert(SUCCEED == ret, "Small File", "Client connect to server");
    
    if (SUCCEED == ret) {
        ret = file_send(&client_sock, test_file, 30);
        test_assert(FILE_TRANS_OK == ret, "Small File", "File send");
        
        if (FILE_TRANS_OK != ret) {
            file_trans_strerror(ret, error_msg, sizeof(error_msg));
            printf("Error: %s\n", error_msg);
            printf("Socket error: %s\n", nt_socket_strerror());
        }
        
        nt_tcp_close(&client_sock);
    }
    
    pthread_join(server_tid, NULL);
    
    char received_file[MAX_STRING_LEN];
    snprintf(received_file, sizeof(received_file), "%s/test_small.bin", TEST_SAVE_DIR);
    
    if (0 == stat(received_file, &st)) {
        printf("Received file size: %ld bytes\n", (long)st.st_size);
        ret = compare_files(test_file, received_file);
        test_assert(SUCCEED == ret, "Small File", "File content match");
    } else {
        test_assert(0, "Small File", "Received file exists");
    }
    
    remove(test_file);
    remove(received_file);
}

static void test_large_file_transfer(void)
{
    nt_socket_t client_sock;
    pthread_t server_tid;
    int ret;
    char test_file[] = "/tmp/test_large.bin";
    struct stat st;
    
    printf("\n=== Test: Large File Transfer ===\n");
    
    ret = create_test_file(test_file, TEST_LARGE_FILE_SIZE);
    test_assert(SUCCEED == ret, "Large File", "Create large test file");
    if (SUCCEED != ret) {
        return;
    }
    
    stat(test_file, &st);
    printf("Created large test file: %s, size: %ld bytes\n", test_file, (long)st.st_size);
    
    pthread_create(&server_tid, NULL, file_server_thread, TEST_SAVE_DIR);
    usleep(200000);
    
    ret = nt_tcp_connect(&client_sock, NULL, "127.0.0.1", TEST_PORT, 10,
            NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
    test_assert(SUCCEED == ret, "Large File", "Client connect to server");
    
    if (SUCCEED == ret) {
        ret = file_send(&client_sock, test_file, 60);
        test_assert(FILE_TRANS_OK == ret, "Large File", "Large file send");
        
        if (FILE_TRANS_OK != ret) {
            printf("Send failed: %d, error: %s\n", ret, nt_socket_strerror());
        }
        
        nt_tcp_close(&client_sock);
    }
    
    pthread_join(server_tid, NULL);
    
    char received_file[MAX_STRING_LEN];
    snprintf(received_file, sizeof(received_file), "%s/test_large.bin", TEST_SAVE_DIR);
    
    if (0 == stat(received_file, &st)) {
        printf("Received file size: %ld bytes\n", (long)st.st_size);
        ret = compare_files(test_file, received_file);
        test_assert(SUCCEED == ret, "Large File", "File content match");
    } else {
        test_assert(0, "Large File", "Received file exists");
    }
    
    remove(test_file);
    remove(received_file);
}

static void test_nonexistent_file(void)
{
    nt_socket_t client_sock;
    pthread_t server_tid;
    int ret;
    
    printf("\n=== Test: Nonexistent File ===\n");
    
    pthread_create(&server_tid, NULL, file_server_thread, TEST_SAVE_DIR);
    usleep(200000);
    
    ret = nt_tcp_connect(&client_sock, NULL, "127.0.0.1", TEST_PORT, 10,
            NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
    test_assert(SUCCEED == ret, "Nonexistent", "Client connect");
    
    if (SUCCEED == ret) {
        ret = file_send(&client_sock, "/tmp/nonexistent_file_12345.bin", 10);
        test_assert(FILE_TRANS_FILE_NOT_FOUND == ret, "Nonexistent", "File not found error");
        
        nt_tcp_close(&client_sock);
    }
    
    pthread_join(server_tid, NULL);
}

static void test_invalid_filename(void)
{
    nt_socket_t sock;
    int ret;
    
    printf("\n=== Test: Invalid Filename ===\n");
    
    ret = send_file_header(&sock, NULL, 100);
    test_assert(FILE_TRANS_INVALID_FILENAME == ret, "Invalid Name", "NULL filename");
    
    ret = send_file_header(&sock, "", 100);
    test_assert(FILE_TRANS_INVALID_FILENAME == ret, "Invalid Name", "Empty filename");
    
    char long_name[MAX_FILENAME_LEN + 10];
    memset(long_name, 'A', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    
    ret = send_file_header(&sock, long_name, 100);
    test_assert(FILE_TRANS_INVALID_FILENAME == ret, "Invalid Name", "Too long filename");
}

static void test_error_messages(void)
{
    char error_msg[MAX_STRING_LEN];
    
    printf("\n=== Test: Error Messages ===\n");
    
    file_trans_strerror(FILE_TRANS_OK, error_msg, sizeof(error_msg));
    printf("Error code %d: %s\n", FILE_TRANS_OK, error_msg);
    test_assert(0 == strcmp(error_msg, "Success"), "Error Msg", "Success message");
    
    file_trans_strerror(FILE_TRANS_FILE_NOT_FOUND, error_msg, sizeof(error_msg));
    printf("Error code %d: %s\n", FILE_TRANS_FILE_NOT_FOUND, error_msg);
    test_assert(0 == strcmp(error_msg, "File not found"), "Error Msg", "File not found message");
    
    file_trans_strerror(FILE_TRANS_NETWORK_ERROR, error_msg, sizeof(error_msg));
    printf("Error code %d: %s\n", FILE_TRANS_NETWORK_ERROR, error_msg);
    test_assert(0 == strcmp(error_msg, "Network error"), "Error Msg", "Network error message");
    
    file_trans_strerror(-999, error_msg, sizeof(error_msg));
    printf("Error code %d: %s\n", -999, error_msg);
    test_assert(0 == strcmp(error_msg, "Unknown error"), "Error Msg", "Unknown error message");
}

static void test_file_transfer_init_cleanup(void)
{
    nt_socket_t sock;
    int ret;
    
    printf("\n=== Test: File Transfer Init/Cleanup ===\n");
    
    ret = file_transfer_init(&sock);
    test_assert(FILE_TRANS_OK == ret, "Init", "File transfer init");
    
    file_transfer_cleanup(&sock);
    test_assert(1, "Cleanup", "File transfer cleanup");
}

static void test_empty_file(void)
{
    nt_socket_t client_sock;
    pthread_t server_tid;
    int ret;
    char test_file[] = "/tmp/test_empty.bin";
    FILE *fp;
    struct stat st;
    
    printf("\n=== Test: Empty File Transfer ===\n");
    
    fp = fopen(test_file, "wb");
    test_assert(NULL != fp, "Empty File", "Create empty test file");
    if (NULL == fp) {
        return;
    }
    fclose(fp);
    
    pthread_create(&server_tid, NULL, file_server_thread, TEST_SAVE_DIR);
    usleep(200000);
    
    ret = nt_tcp_connect(&client_sock, NULL, "127.0.0.1", TEST_PORT, 10,
            NT_TCP_SEC_UNENCRYPTED, NULL, NULL);
    test_assert(SUCCEED == ret, "Empty File", "Client connect to server");
    
    if (SUCCEED == ret) {
        ret = file_send(&client_sock, test_file, 10);
        test_assert(FILE_TRANS_OK == ret, "Empty File", "Empty file send");
        
        nt_tcp_close(&client_sock);
    }
    
    pthread_join(server_tid, NULL);
    
    char received_file[MAX_STRING_LEN];
    snprintf(received_file, sizeof(received_file), "%s/test_empty.bin", TEST_SAVE_DIR);
    
    if (0 == stat(received_file, &st)) {
        printf("Received empty file size: %ld bytes\n", (long)st.st_size);
        test_assert(0 == st.st_size, "Empty File", "Empty file received");
    } else {
        test_assert(0, "Empty File", "Received file exists");
    }
    
    remove(test_file);
    remove(received_file);
}

static void test_directory_creation(void)
{
    printf("\n=== Test: Directory Creation ===\n");
    
    int ret = mkdir(TEST_SAVE_DIR, 0755);
    if (0 == ret) {
        printf("Created directory: %s\n", TEST_SAVE_DIR);
        test_assert(1, "Dir Create", "Create test directory");
    } else if (EEXIST == errno) {
        printf("Directory already exists: %s\n", TEST_SAVE_DIR);
        test_assert(1, "Dir Create", "Directory exists");
    } else {
        test_assert(0, "Dir Create", "Create test directory");
    }
}

int main(int argc, char **argv)
{
    printf("========================================\n");
    printf("File Transfer Tests\n");
    printf("========================================\n");
    
    test_error_messages();
    test_file_transfer_init_cleanup();
    test_invalid_filename();
    test_directory_creation();
    test_empty_file();
    test_small_file_transfer();
    test_large_file_transfer();
    test_nonexistent_file();
    
    printf("\n========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");
    printf("Passed: %d\n", test_passed);
    printf("Failed: %d\n", test_failed);
    printf("Total:  %d\n", test_passed + test_failed);
    printf("========================================\n");
    
    return test_failed > 0 ? 1 : 0;
}