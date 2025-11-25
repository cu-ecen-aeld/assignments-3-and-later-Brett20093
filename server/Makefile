CC = gcc
CROSS_COMPILE ?=
TARGET = aesdsocket
SRCS = aesdsocket.c
HDRS = aesdsocket.h
OBJS = $(SRCS:.c=.o)
CFLAGS = -Wall -Werror

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CROSS_COMPILE)$(CC) $(OBJS) -o $(TARGET)

%.o: %.c %.h
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)