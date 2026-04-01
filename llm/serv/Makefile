CC = gcc
CFLAGS = -Wall -g -O2 -I.
LDFLAGS = -lpthread

SRCS_COMMON = common.c comms.c
SRCS_FILE = file_transfer.c
SRCS_TEST_NET = test_comms.c
SRCS_TEST_FILE = test_file_transfer.c
SRCS_TEST_SIMPLE = test_file_simple.c
SRCS_SERV = ftran_serv.c
SRCS_CLIE = ftran_clie.c
SRCS_LLM_SERV = llm_serv.c
SRCS_LLM_CLIE = llm_client.c
SRCS_CLIENT = client.c

OBJS_COMMON = $(SRCS_COMMON:.c=.o)
OBJS_FILE = $(SRCS_FILE:.c=.o)
OBJS_TEST_NET = $(SRCS_TEST_NET:.c=.o)
OBJS_TEST_FILE = $(SRCS_TEST_FILE:.c=.o)
OBJS_TEST_SIMPLE = $(SRCS_TEST_SIMPLE:.c=.o)
OBJS_SERV = $(SRCS_SERV:.c=.o)
OBJS_CLIE = $(SRCS_CLIE:.c=.o)
OBJS_LLM_SERV = $(SRCS_LLM_SERV:.c=.o)
OBJS_LLM_CLIE = $(SRCS_LLM_CLIE:.c=.o)
OBJS_CLIENT = $(SRCS_CLIENT:.c=.o)

TARGET_NET = net_test
TARGET_FILE = file_test
TARGET_SIMPLE = simple_test
TARGET_SERV = ftran_serv
TARGET_CLIE = ftran_clie
TARGET_LLM_SERV = llm_serv
TARGET_LLM_CLIE = llm_client
TARGET_CLIENT = client

.PHONY: all clean test

all: $(TARGET_NET) $(TARGET_SIMPLE) $(TARGET_SERV) $(TARGET_CLIE) $(TARGET_LLM_SERV) $(TARGET_LLM_CLIE) $(TARGET_CLIENT)

$(TARGET_NET): $(OBJS_COMMON) $(OBJS_TEST_NET)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_FILE): $(OBJS_COMMON) $(OBJS_FILE) $(OBJS_TEST_FILE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_SIMPLE): $(OBJS_COMMON) $(OBJS_FILE) $(OBJS_TEST_SIMPLE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_SERV): $(OBJS_COMMON) $(OBJS_FILE) $(OBJS_SERV)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_CLIE): $(OBJS_COMMON) $(OBJS_FILE) $(OBJS_CLIE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_LLM_SERV): $(OBJS_COMMON) $(OBJS_LLM_SERV)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_LLM_CLIE): $(OBJS_COMMON) $(OBJS_LLM_CLIE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_CLIENT): $(OBJS_COMMON) $(OBJS_CLIENT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS_COMMON) $(OBJS_FILE) $(OBJS_TEST_NET) $(OBJS_TEST_FILE) $(OBJS_TEST_SIMPLE) $(OBJS_SERV) $(OBJS_CLIE) $(OBJS_LLM_SERV) $(OBJS_LLM_CLIE) $(OBJS_CLIENT)
	rm -f $(TARGET_NET) $(TARGET_FILE) $(TARGET_SIMPLE) $(TARGET_SERV) $(TARGET_CLIE) $(TARGET_LLM_SERV) $(TARGET_LLM_CLIE) $(TARGET_CLIENT)
	rm -rf /tmp/nt_file_test
	rm -f /tmp/test_*.bin

test: $(TARGET_NET) $(TARGET_SIMPLE)
	@echo "=== Running Network Tests ==="
	@./$(TARGET_NET)
	@echo ""
	@echo "=== Running File Transfer Tests ==="
	@./$(TARGET_SIMPLE)

install: $(TARGET_SERV) $(TARGET_CLIE)
	@echo "Installing file transfer tools..."
	mkdir -p /usr/local/bin
	mkdir -p /etc
	mkdir -p /var/ftran/uploads
	mkdir -p /var/log
	cp $(TARGET_SERV) /usr/local/bin/
	cp $(TARGET_CLIE) /usr/local/bin/
	chmod 755 /usr/local/bin/$(TARGET_SERV)
	chmod 755 /usr/local/bin/$(TARGET_CLIE)
	@echo "Installation complete"
	@echo "  Server: /usr/local/bin/$(TARGET_SERV)"
	@echo "  Client: /usr/local/bin/$(TARGET_CLIE)"
