CC = gcc
CROSS_COMPILE ?= aarch64-none-linux-gnu-
TARGET = writer
SRCS = writer.c
OBJS = $(SRCS:.c=.o)
CFLAGS = -Wall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CROSS_COMPILE)$(CC) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)