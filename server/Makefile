CC ?= gcc
CROSS_COMPILE ?=
TARGET = aesdsocket
SRCS = aesdsocket.c connection_thread.c
HDRS = aesdsocket.h connection_thread.h queue.h aesd_ioctl.h
OBJS = $(SRCS:.c=.o)
LDFLAGS ?= -lc -lpthread
INCLUDE_DIR ?= ../aesd-char-driver
CFLAGS ?= -Wall -Werror -I$(INCLUDE_DIR)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $(TARGET)

%.o: %.c %.h
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)