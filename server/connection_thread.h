#include <stdbool.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

struct connection_thread{
    pthread_mutex_t *file_mutex;
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len;
    int output_fd;
    bool thread_complete_success;
    bool thread_complete;
};
