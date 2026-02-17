#include "connection_thread.h"
#include "aesd_ioctl.h"
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
        printf("Receiving from client...\n");
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

        printf("Message received: %s\n", recv_buffer);
        struct aesd_seekto seekto;
        seekto.write_cmd = 0;
        seekto.write_cmd_offset = 0;
        if (check_for_ioctl_command(&seekto, recv_buffer, received_size) == 0)
        {
            printf("attempting ioctl...\n");
            if (ioctl(output_fd, AESDCHAR_IOCSEEKTO, &seekto) < 0)
            {
                printf("ERROR: ioctl()\n");
                syslog(LOG_DEBUG, "ioctl() error");
                return -1;
            }
            printf("ioctl done.\n");
        }
        else
        {
            printf("writing to device...");
            int write_return = write(output_fd, recv_buffer, index+1);
            if (write_return == -1)
            {
                syslog(LOG_ERR, "write error: %s", strerror(errno));
                return -1;
            }
            printf("Done writing to device.");
        }
        printf("Done processing message %s\n", recv_buffer);
        syslog(LOG_INFO, "Message received with sizeof %d", (int)received_size);
    } while (received_size >= BUFFER_SIZE);

    return 0;
}

int check_for_ioctl_command(struct aesd_seekto* seekto, char *recv_buffer, ssize_t received_size)
{
    printf("check_for_ioctl_command1\n");
    char* first_num_start = strchr(recv_buffer, ':');
    printf("check_for_ioctl_command1.2\n");
    if (first_num_start == NULL) 
    {
        printf("check_for_ioctl_command1.3\n");
        return -1;
    }
    printf("check_for_ioctl_command2\n");
    char* first_num_end = strchr(first_num_start, ',');
    if (first_num_end == NULL) 
    {
        return -1;
    }
    printf("check_for_ioctl_command3\n");
    char* second_num_end = strchr(first_num_end, '\n');
    if (second_num_end == NULL)
    {
        return -1;
    }
    size_t num_size = first_num_end - first_num_start - 1;
    char first_num_str[10];
    memset(first_num_str, 0, 10);
    printf("check_for_ioctl_command4\n");
    strncpy(first_num_str, first_num_start+1, num_size);
    printf("first_num_str: %s\n", first_num_str);
    seekto->write_cmd = atoi(first_num_str);

    char second_num_str[10];
    num_size = second_num_end - first_num_end - 1;
    memset(second_num_str, 0, 10);
    printf("check_for_ioctl_command5\n");
    strncpy(second_num_str, first_num_end+1, num_size);
    printf("second_num_str: %s\n", second_num_str);
    seekto->write_cmd_offset = atoi(second_num_str);

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
    printf("opening device...\n");
    int output_fd = open(outputfile_name, O_RDWR, 0666);
    if (output_fd < 0) 
    {
        printf("Open output file error: %s\n", strerror(errno));
        syslog(LOG_ERR, "Open output file error: %s", strerror(errno));
        unlock_mutex(connection_data->file_mutex);
        return thread_param;
    }
    printf("Device opened.\n");
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
    printf("Device closed.\n");
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
