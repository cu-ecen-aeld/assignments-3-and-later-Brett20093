#include <stdbool.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Optional: use these functions to add debug or error prints to your application
//#define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

struct connection_thread_args{
    pthread_mutex_t *file_mutex;
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len;
    int output_fd;
    bool thread_complete_success;
    bool thread_complete;
};

void lock_mutex(pthread_mutex_t *file_mutex);

void unlock_mutex(pthread_mutex_t *file_mutex);

void* connection_thread(void* thread_param);
