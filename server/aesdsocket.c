#include <syslog.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "aesdsocket.h"

int main(int argc, char *argv[])
{
    openlog(NULL, 0, LOG_USER);

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    printf("socket_fd: %d\n", socket_fd);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = 0;
    printf("Setting up address info...\n");
    if ((status = getaddrinfo(NULL, "9000", &hints, &res)) != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return -1;
    }
    printf("Address info setup.\n");

    printf("Binding socket...\n");
    if ((status = bind(socket_fd, res->ai_addr, sizeof(struct sockaddr))) != 0)
    {
        fprintf(stderr, "bind error: %s\n", gai_strerror(status));
        return -1;
    }
    printf("Socket bound.\n");

    freeaddrinfo(res);

    if ((status = listen(socket_fd, 2)) != 0)
    {
        fprintf(stderr, "listen error: %s\n", gai_strerror(status));
        return -1;
    }

    return 0;
}