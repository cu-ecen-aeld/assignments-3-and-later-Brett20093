#include "connection_thread.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define BUFFER_SIZE 1024

const char *outputfile_name = "/dev/aesdchar";

void lock_mutex(pthread_mutex_t *file_mutex)
{
    int rc = pthread_mutex_lock(file_mutex);
    if(rc != 0) 
    {
        ERROR_LOG("pthread_mutex_lock");
    }
}

void unlock_mutex(pthread_mutex_t *file_mutex)
{
    int rc = pthread_mutex_unlock(file_mutex);
    if(rc != 0) {
        ERROR_LOG("pthread_mutex_unlock");
    }
}

int recv_messages(char *recv_buffer, int client_fd, int output_fd)
{
    ssize_t received_size = 0;
    bool mutex_set = false;

    do
    {
        syslog(LOG_INFO, "Receiving from client...");
        memset(recv_buffer, 0, BUFFER_SIZE);
        received_size = recv(client_fd, recv_buffer, BUFFER_SIZE, 0);
        if (!mutex_set)
        {
            mutex_set = true;
        }
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

int send_messages(char *send_buffer, int client_fd, int output_fd)
{
    int bytes_read = BUFFER_SIZE;
    while (bytes_read > 0)
    {
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
    }

    return 0;
}

void* connection_thread(void* thread_param)
{
    char recv_buffer[BUFFER_SIZE];
    char send_buffer[BUFFER_SIZE];

    struct connection_thread_args* connection_data = (struct connection_thread_args *) thread_param;

    connection_data->thread_complete = false;

    lock_mutex(connection_data->file_mutex);
    int output_fd = open(outputfile_name, O_RDWR, 0666);
    if (output_fd < 0) 
    {
        syslog(LOG_ERR, "Open output file error: %s", strerror(errno));
        unlock_mutex(connection_data->file_mutex);
        return thread_param;
    }
    // Receiving logic to handle large messages
    if (recv_messages(recv_buffer, connection_data->client_fd, output_fd) == -1)
    {
        close(connection_data->client_fd);
        connection_data->thread_complete_success = false;
        connection_data->thread_complete = true;
        close(output_fd);
        unlock_mutex(connection_data->file_mutex);
        return thread_param;
    }

    if (send_messages(send_buffer, connection_data->client_fd, output_fd) == -1)
    {
        close(connection_data->client_fd);
        connection_data->thread_complete_success = false;
        connection_data->thread_complete = true;
        close(output_fd);
        unlock_mutex(connection_data->file_mutex);
        return thread_param;
    }
    close(output_fd);
    unlock_mutex(connection_data->file_mutex);

    // Close the client socket and log it
    close(connection_data->client_fd);
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(connection_data->client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Closed connection from %s", ip_str);

    connection_data->thread_complete_success = true;
    connection_data->thread_complete = true;

    return thread_param;
}
