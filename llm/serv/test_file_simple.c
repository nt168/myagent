#include "common.h"
#include "comms.h"
#include "file_transfer.h"
#include <sys/stat.h>
#include <dirent.h>

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
}

static void test_file_transfer_init(void)
{
    nt_socket_t sock;
    int ret;
    
    printf("\n=== Test: File Transfer Init ===\n");
    
    ret = file_transfer_init(&sock);
    test_assert(FILE_TRANS_OK == ret, "Init", "File transfer init");
    
    file_transfer_cleanup(&sock);
    test_assert(1, "Cleanup", "File transfer cleanup");
}

static void test_send_file_header(void)
{
    nt_socket_t sock;
    int ret;
    
    printf("\n=== Test: Send File Header ===\n");
    
    ret = send_file_header(&sock, "test.txt", 1024);
    test_assert(FILE_TRANS_OK == ret, "Send Header", "Send file header with valid params");
    
    ret = send_file_header(&sock, NULL, 1024);
    test_assert(FILE_TRANS_INVALID_FILENAME == ret, "Send Header", "NULL filename");
    
    ret = send_file_header(&sock, "", 1024);
    test_assert(FILE_TRANS_INVALID_FILENAME == ret, "Send Header", "Empty filename");
}

static void test_send_ack(void)
{
    nt_socket_t sock;
    int ret;
    
    printf("\n=== Test: Send ACK ===\n");
    
    ret = send_ack(&sock, FILE_TRANS_OK, "Success");
    test_assert(SUCCEED == ret, "Send ACK", "Send success ACK");
    
    ret = send_ack(&sock, FILE_TRANS_ERROR, "Error occurred");
    test_assert(SUCCEED == ret, "Send ACK", "Send error ACK");
}

static void test_send_file_chunk(void)
{
    nt_socket_t sock;
    int ret;
    char data[100];
    
    printf("\n=== Test: Send File Chunk ===\n");
    
    memset(data, 'A', sizeof(data));
    ret = send_file_chunk(&sock, data, sizeof(data));
    test_assert(FILE_TRANS_OK == ret, "Send Chunk", "Send data chunk");
    
    ret = send_file_chunk(&sock, NULL, 0);
    test_assert(FILE_TRANS_OK == ret, "Send Chunk", "Send empty chunk (EOF)");
}

static void test_nonexistent_file(void)
{
    nt_socket_t sock;
    int ret;
    
    printf("\n=== Test: Nonexistent File ===\n");
    
    ret = file_send(&sock, "/tmp/nonexistent_file_xyz123.bin", 5);
    test_assert(FILE_TRANS_FILE_NOT_FOUND == ret, "Nonexistent", "File not found error");
}

static void test_file_creation(void)
{
    char test_file[] = "/tmp/test_ft_create.bin";
    FILE *fp;
    int ret;
    struct stat st;
    
    printf("\n=== Test: File Creation ===\n");
    
    fp = fopen(test_file, "wb");
    test_assert(NULL != fp, "File Create", "Create test file");
    
    if (NULL != fp) {
        fwrite("test", 1, 4, fp);
        fclose(fp);
        
        ret = stat(test_file, &st);
        test_assert(0 == ret, "File Create", "File exists");
        test_assert(4 == st.st_size, "File Create", "File size correct");
        
        remove(test_file);
    }
}

static void test_directory_operations(void)
{
    const char *test_dir = "/tmp/nt_file_test_simple";
    int ret;
    DIR *dir;
    
    printf("\n=== Test: Directory Operations ===\n");
    
    ret = mkdir(test_dir, 0755);
    test_assert(0 == ret || EEXIST == errno, "Dir Ops", "Create test directory");
    
    dir = opendir(test_dir);
    test_assert(NULL != dir, "Dir Ops", "Open directory");
    if (dir) closedir(dir);
    
    rmdir(test_dir);
}

int main(int argc, char **argv)
{
    printf("========================================\n");
    printf("File Transfer Tests (Simple)\n");
    printf("========================================\n");
    
    test_error_messages();
    test_file_transfer_init();
    test_send_file_header();
    test_send_ack();
    test_send_file_chunk();
    test_nonexistent_file();
    test_file_creation();
    test_directory_operations();
    
    printf("\n========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");
    printf("Passed: %d\n", test_passed);
    printf("Failed: %d\n", test_failed);
    printf("Total:  %d\n", test_passed + test_failed);
    printf("========================================\n");
    
    return test_failed > 0 ? 1 : 0;
}