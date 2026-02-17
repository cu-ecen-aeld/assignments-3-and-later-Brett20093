#include <syslog.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#include "queue.h"
#include "connection_thread.h"

// SLIST.
typedef struct slist_data_s slist_data_t;
struct slist_data_s {
    struct connection_thread_args *connection;
    pthread_t *thread;
    SLIST_ENTRY(slist_data_s) entries;
};

const char *timestamp_tag = "timestamp:";
static volatile sig_atomic_t quit = 0;

void stop_process(int socket_fd)
{
    if (socket_fd >= 0)
    {
        close(socket_fd);
    }
}

static void shutdown_handler(int signal_number)
{
    if (signal_number == SIGINT || signal_number == SIGTERM)
    {
        quit = 1;
    }
}

void setup_handlers()
{
    struct sigaction shutdown_action;
    memset(&shutdown_action, 0, sizeof(struct sigaction));
    shutdown_action.sa_handler = shutdown_handler;
    if (sigaction(SIGINT, &shutdown_action, NULL) != 0)
    {
        syslog(LOG_ERR, "Failed to add SIGINT to sigaction: %s", strerror(errno));
    }
    if (sigaction(SIGTERM, &shutdown_action, NULL) != 0)
    {
        syslog(LOG_ERR, "Failed to add SIGTERM to sigaction: %s", strerror(errno));
    }
}

int run_server(int socket_fd)
{
    pthread_mutex_t *file_mutex = malloc(sizeof(pthread_mutex_t));
    if (file_mutex == NULL) {
        syslog(LOG_ERR, "Failed to setup mutex.");
        return -1;
    }
    pthread_mutex_init(file_mutex, NULL);

    slist_data_t *datap=NULL;

    SLIST_HEAD(slisthead, slist_data_s) head;
    SLIST_INIT(&head);

    // Setup the socket to listen
    int status;
    syslog(LOG_INFO, "Setting up listener...");
    if ((status = listen(socket_fd, 2)) != 0)
    {
        syslog(LOG_ERR, "Listen error: %s", gai_strerror(status));
        return -1;
    }
    syslog(LOG_INFO, "Socket is listening.");

    // Initialize the address structure for the client
    struct sockaddr_in client_addr;
    socklen_t client_len;
    client_len = sizeof(client_addr);
    
    // Main loop
    while (!quit)
    {
        struct slist_data_s *tmp;
        SLIST_FOREACH_SAFE(datap, &head, entries, tmp) {
            if (datap->connection->thread_complete) {
                pthread_join(*(datap->thread), NULL);   // note the dereference
                free(datap->thread);
                free(datap->connection);
                SLIST_REMOVE(&head, datap, slist_data_s, entries);
                free(datap);
            }
        }

        // Wait for a client to send a message
        syslog(LOG_INFO, "Waiting to accept a message...");
        int client_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            else {
                syslog(LOG_ERR, "Accept error: %s", strerror(errno));
                continue;
            }
        }

        // Convert the client address structure to a human readable IPv4 and log it
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", ip_str);
        printf("Accepted connection from %s\n", ip_str);

        struct connection_thread_args *tData;
        printf("tData malloc...\n");
        tData = (struct connection_thread_args *)malloc(sizeof(struct connection_thread_args));
        if (tData == NULL) {
            printf("connection_thread_args memory allocation failed\n");
            syslog(LOG_ERR, "connection_thread_args memory allocation failed");
            quit = 1;
            continue;
        }

        printf("thread malloc...\n");
        pthread_t *thread = malloc(sizeof(pthread_t));
        if (thread == NULL) {
            printf("pthread_t memory allocation failed\n");
            ERROR_LOG("pthread_t memory allocation failed");
            quit = 1;
            continue;
        }
        
        tData->client_addr = client_addr;
        tData->client_fd = client_fd;
        tData->client_len = client_len;
        tData->file_mutex = file_mutex;
        tData->thread_complete = false;
        tData->thread_complete_success = false;

        printf("pthread_create...\n");
        int rc = pthread_create(thread, NULL, connection_thread, tData);
        if(rc != 0) {
            printf("error: pthread_create\n");
            ERROR_LOG("pthread_create");
            quit = 1;
            continue;
        }

        datap = malloc(sizeof(slist_data_t));
        datap->connection = tData;
        datap->thread = thread;
        SLIST_INSERT_HEAD(&head, datap, entries);
    }

    while (!SLIST_EMPTY(&head)) {
        datap = SLIST_FIRST(&head);
        close(datap->connection->client_fd);
        pthread_join(*(datap->thread), NULL);
        free(datap->thread);
        free(datap->connection);
        SLIST_REMOVE_HEAD(&head, entries);
        free(datap);
    }

    pthread_mutex_destroy(file_mutex);
    free(file_mutex);
    
    return 0;
}

int main(int argc, char *argv[])
{
    bool is_daemon = false;
    // Check for the -d daemon argument
    char *daemon_arg = NULL;
    if (argc > 1)
    {
        daemon_arg = argv[1];
        if (strcmp(daemon_arg, "-d") == 0)
        {
            is_daemon = true;
        }
    }

    // Handlers for catching termination signals
    setup_handlers();

    openlog(NULL, 0, LOG_USER);

    // Open socket
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        syslog(LOG_ERR, "socket error: %s", strerror(errno));
        
        stop_process(socket_fd);
        return -1;
    }
    syslog(LOG_INFO, "socket_fd: %d", socket_fd);

    // Add the ability to resuse a address that might be in use
    int optval = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 ) {
        syslog(LOG_ERR, "setsockopt error: %s", strerror(errno));
        stop_process(socket_fd);
    }

    int flags = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);

    // Setup server address info
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = 0;
    syslog(LOG_INFO, "Setting up address info...");
    if ((status = getaddrinfo(NULL, "9000", &hints, &res)) != 0)
    {
        syslog(LOG_ERR, "getaddrinfo error: %s", gai_strerror(status));
        stop_process(socket_fd);
        return -1;
    }
    syslog(LOG_INFO, "Address info setup.");

    // Bind the server socket to the internet address
    syslog(LOG_INFO, "Binding socket...");
    if ((status = bind(socket_fd, res->ai_addr, res->ai_addrlen)) != 0)
    {
        syslog(LOG_ERR, "bind error: %s", strerror(errno));
        stop_process(socket_fd);
        return -1;
    }
    syslog(LOG_INFO, "Socket bound.");

    // Freeup the memory for the address
    freeaddrinfo(res);

    int ret = 0;
    if (is_daemon)
    {
        fflush(stdout);
        pid_t pid = fork();
        if (pid < 0)
        {
            syslog(LOG_ERR, "fork error: %s\n", strerror(errno));
            return false;
        }
        else if (pid == 0)
        {
            ret = run_server(socket_fd);
        }
    }
    else
    {
        ret = run_server(socket_fd);
    }

    stop_process(socket_fd);

    return ret;
}