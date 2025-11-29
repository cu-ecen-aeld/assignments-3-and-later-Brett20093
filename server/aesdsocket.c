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

#define BUFFER_SIZE 1024

const char *outputfile_name = "/var/tmp/aesdsocketdata";
int output_fd = -1;
int socket_fd = -1;
int client_fd = -1;
bool process_running = true;
bool process_stopped = false;
bool is_daemon = false;

void stop_process()
{
    if (!process_stopped)
    {
        process_running = false;
        if (client_fd >= 0)
        {
            close(client_fd);
        }
        if (socket_fd >= 0)
        {
            close(socket_fd);
        }
        if (output_fd >= 0)
        {
            close(output_fd);
        }
        remove(outputfile_name);
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
        syslog(LOG_ERR, "Failed to add SIGINT to sigaction: %s", strerror(errno));
    }
    if (sigaction(SIGTERM, &shutdown_action, NULL) != 0)
    {
        syslog(LOG_ERR, "Failed to add SIGTERM to sigaction: %s", strerror(errno));
    }
}

int recv_messages(char *recv_buffer)
{
    ssize_t received_size = 0;
    do
    {
        syslog(LOG_INFO, "Receiving from client...");
        memset(recv_buffer, 0, BUFFER_SIZE);
        received_size = recv(client_fd, recv_buffer, BUFFER_SIZE, 0);
        if (received_size == 0)
        {
            syslog(LOG_ERR, "The client has closed");
            return -1;
        }
        if (received_size < 0)
        {
            syslog(LOG_ERR, "recv error: %s", strerror(errno));
            return -1;
        }
        char *ptr = memchr(recv_buffer, '\n', received_size);
        int index = 0;
        if (ptr != NULL)
        {
            index = ptr - recv_buffer;
        }
        else
        {
            index = received_size - 1;
        }
        int write_return = write(output_fd, recv_buffer, index+1);
        if (write_return == -1)
        {
            syslog(LOG_ERR, "write error: %s", strerror(errno));
            return -1;
        }
        syslog(LOG_INFO, "Message received with sizeof %d", (int)received_size);
    } while (received_size >= BUFFER_SIZE);

    return 0;
}

int send_messages(char *send_buffer)
{
    if (lseek(output_fd, 0, SEEK_SET) < 0) 
    {
        syslog(LOG_ERR, "lseek error: %s", strerror(errno));
        return -1;
    }
    int bytes_read = 0;
    do
    {
        bytes_read = read(output_fd, send_buffer, BUFFER_SIZE);
        if (bytes_read < 0)
        {
            syslog(LOG_ERR, "read error: %s", strerror(errno));
            return -1;
        }
        int sent_bytes = send(client_fd, send_buffer, bytes_read, 0);
        if (sent_bytes < 0)
        {
            syslog(LOG_ERR, "send error: %s", strerror(errno));
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
    syslog(LOG_INFO, "Setting up listener...");
    if ((status = listen(socket_fd, 2)) != 0)
    {
        syslog(LOG_ERR, "Listen error: %s", gai_strerror(status));
        stop_process();
        return -1;
    }
    syslog(LOG_INFO, "Socket is listening.");

    // Initialize the address structure for the client
    struct sockaddr_in client_addr;
    socklen_t client_len;
    client_len = sizeof(client_addr);

    // Main loop
    while (process_running)
    {
        // Wait for a client to send a message
        syslog(LOG_INFO, "Waiting to accept a message...");
        client_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) 
        {
            syslog(LOG_ERR, "Accept error: %s", strerror(errno));
            stop_process();
            return -1;
        }

        // Convert the client address structure to a human readable IPv4 and log it
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", ip_str);

        // Receiving logic to handle large messages
        if (recv_messages(recv_buffer) == -1)
        {
            stop_process();
            return -1;
        }

        // Sending logic to handle a large file
        if (send_messages(send_buffer) == -1)
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
            syslog(LOG_INFO, "Running as a daemon...");
        }
    }

    // Handlers for catching termination signals
    setup_handlers();

    openlog(NULL, 0, LOG_USER);

    // Open output file
    output_fd = open(outputfile_name, O_CREAT | O_RDWR, 0644);
    if (output_fd < 0) 
    {
        syslog(LOG_ERR, "Open output file error: %s", strerror(errno));
        return -1;
    }

    // Open socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        syslog(LOG_ERR, "socket error: %s", strerror(errno));
        
        stop_process();
        return -1;
    }
    syslog(LOG_INFO, "socket_fd: %d", socket_fd);

    // Add the ability to resuse a address that might be in use
    int optval = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 ) {
        syslog(LOG_ERR, "setsockopt error: %s", strerror(errno));
        stop_process();
    }

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
        stop_process();
        return -1;
    }
    syslog(LOG_INFO, "Address info setup.");

    // Bind the server socket to the internet address
    syslog(LOG_INFO, "Binding socket...");
    if ((status = bind(socket_fd, res->ai_addr, res->ai_addrlen)) != 0)
    {
        syslog(LOG_ERR, "bind error: %s", strerror(errno));
        stop_process();
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
            syslog(LOG_ERR, "fork error: %s", strerror(errno));
            return false;
        }
        else if (pid == 0)
        {
            ret = run_server();
        }
        else
        {
            exit(0);
        }
    }
    else
    {
        ret = run_server();
    }

    return ret;
}