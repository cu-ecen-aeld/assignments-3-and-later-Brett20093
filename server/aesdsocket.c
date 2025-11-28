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

const int BUFFER_SIZE = 1024;
const char *outputfile_name = "/var/tmp/aesdsocketdata";
int output_fd;
int socket_fd;
int client_fd;
bool process_running = true;
bool process_stopped = false;
bool is_daemon = false;

void stop_process()
{
    if (!process_stopped)
    {
        process_running = false;
        close(output_fd);
        close(socket_fd);
        close(client_fd);
        if (remove(outputfile_name) != 0) {
            fprintf(stderr, "error deleteing file %s: %s\n", outputfile_name, strerror(errno));
        }
        process_stopped = true;
    }
}

static void shutdown_handler(int signal_number)
{
    if (signal_number == SIGINT || signal_number == SIGTERM)
    {
        syslog(LOG_INFO, "Caught signal, exiting");
        stop_process();
    }
}

void setup_handlers()
{
    struct sigaction shutdown_action;
    memset(&shutdown_action, 0, sizeof(struct sigaction));
    shutdown_action.sa_handler = shutdown_handler;
    if (sigaction(SIGINT, &shutdown_action, NULL) != 0)
    {
        fprintf(stderr, "Failed to add SIGINT to sigaction: %s\n", strerror(errno));
    }
    if (sigaction(SIGTERM, &shutdown_action, NULL) != 0)
    {
        fprintf(stderr, "Failed to add SIGTERM to sigaction: %s\n", strerror(errno));
    }
}

int recv_messages(char *recv_buffer, int buffer_size)
{
    int received_size = 0;
    do
    {
        printf("Receiving from client...\n");
        received_size = recv(client_fd, recv_buffer, buffer_size, 0);
        if (received_size < 0)
        {
            fprintf(stderr, "recv error: %s\n", strerror(errno));
            return -1;
        }
        char *ptr = strchr(recv_buffer, '\n');
        int index = BUFFER_SIZE - 1;
        if (ptr != NULL)
        {
            printf("No newline found!\n");
            index = ptr - recv_buffer;
        }
        printf("index %d\n", index);
        int write_return = write(output_fd, recv_buffer, index+1);
        if (write_return == -1)
        {
            fprintf(stderr, "write error: %s\n", strerror(errno));
            return -1;
        }
        printf("Message received with sizeof %d: %s\n", received_size, recv_buffer);
    } while (received_size >= BUFFER_SIZE);

    return 0;
}

int send_messages(char *send_buffer, int buffer_size)
{
    if (lseek(output_fd, 0, SEEK_SET) < 0) 
    {
        fprintf(stderr, "lseek error: %s\n", strerror(errno));
        return -1;
    }
    int bytes_read = 0;
    do
    {
        bytes_read = read(output_fd, send_buffer, buffer_size);
        if (bytes_read < 0)
        {
            fprintf(stderr, "read error: %s\n", strerror(errno));
            return -1;
        }
        int sent_bytes = send(client_fd, send_buffer, bytes_read, 0);
        if (sent_bytes < 0)
        {
            fprintf(stderr, "send error: %s\n", strerror(errno));
            return -1;
        }
    } while (bytes_read >= BUFFER_SIZE);

    return 0;
}

int run_server()
{
    char recv_buffer[BUFFER_SIZE];
    char send_buffer[BUFFER_SIZE];

    // Setup the socket to listen
    int status;
    printf("Setting up listener...\n");
    if ((status = listen(socket_fd, 2)) != 0)
    {
        fprintf(stderr, "Listen error: %s\n", gai_strerror(status));
        stop_process();
        return -1;
    }
    printf("Socket is listening.\n");

    // Initialize the address structure for the client
    struct sockaddr_in client_addr;
    socklen_t client_len;
    client_len = sizeof(client_addr);

    // Main loop
    while (process_running)
    {
        // Wait for a client to send a message
        printf("Waiting to accept a message...\n");
        client_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) 
        {
            fprintf(stderr, "Accept error: %s\n", strerror(errno));
            stop_process();
            return -1;
        }

        // Convert the client address structure to a human readable IPv4 and log it
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        printf("Accepted connection from %s\n", ip_str);
        syslog(LOG_INFO, "Accepted connection from %s", ip_str);

        // Receiving logic to handle large messages
        if (recv_messages(recv_buffer, BUFFER_SIZE) == -1)
        {
            stop_process();
            return -1;
        }

        // Sending logic to handle a large file
        if (send_messages(send_buffer, BUFFER_SIZE) == -1)
        {
            stop_process();
            return -1;
        }

        // Close the client socket and log it
        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", ip_str);
    }

    stop_process();
    
    return 0;
}

int main(int argc, char *argv[])
{
    // Check for the -d daemon argument
    char *daemon_arg = NULL;
    if (argc > 1)
    {
        daemon_arg = argv[1];
        if (strcmp(daemon_arg, "-d") == 0)
        {
            is_daemon = true;
            printf("Running as a daemon...\n");
        }
    }

    // Handlers for catching termination signals
    setup_handlers();

    openlog(NULL, 0, LOG_USER);

    // Open output file
    output_fd = open(outputfile_name, O_CREAT | O_RDWR, 0644);
    if (output_fd < 0) 
    {
        fprintf(stderr, "Open output file error: %s\n", strerror(errno));
        return -1;
    }

    // Open socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        fprintf(stderr, "socket error: %s\n", strerror(errno));
        stop_process();
        return -1;
    }
    printf("socket_fd: %d\n", socket_fd);

    // Add the ability to resuse a address that might be in use
    int optval = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 ) {
        fprintf(stderr, "setsockopt error: %s\n", strerror(errno));
        stop_process();
    }

    // Setup server address info
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
        stop_process();
        return -1;
    }
    printf("Address info setup.\n");

    // Bind the server socket to the internet address
    printf("Binding socket...\n");
    if ((status = bind(socket_fd, res->ai_addr, sizeof(struct sockaddr))) != 0)
    {
        fprintf(stderr, "bind error: %s\n", gai_strerror(status));
        stop_process();
        return -1;
    }
    printf("Socket bound.\n");

    // Freeup the memory for the address
    freeaddrinfo(res);

    int ret = 0;
    if (is_daemon)
    {
        fflush(stdout);
        pid_t pid = fork();
        if (pid < 0)
        {
            fprintf(stderr, "fork error: %s\n", strerror(errno));
            return false;
        }
        else if (pid == 0)
        {
            ret = run_server();
        }
    }
    else
    {
        ret = run_server();
    }

    return ret;
}