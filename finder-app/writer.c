#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

const uint8_t EXPECTED_NUM_ARGUMENTS = 3;

int main(int argc, char *argv[])
{
    openlog(NULL, 0, LOG_USER);

    if (argc != EXPECTED_NUM_ARGUMENTS)
    {
        printf("ERROR: Invalid Number of arguments. %d arguments are expected but %d were given.\n", EXPECTED_NUM_ARGUMENTS-1, argc-1);
        syslog(LOG_ERR, "Invalid Number of arguments. %d arguments are expected but %d were given.", EXPECTED_NUM_ARGUMENTS-1, argc-1);
        printf("Usage: %s <writefile> <searchstr>\n", argv[0]);
        syslog(LOG_INFO, "Usage: %s <writefile> <searchstr>", argv[0]);
        exit(1);
    }

    char* write_file = argv[1];
    char* write_str = argv[2];

    int fd = open(write_file, O_CREAT | O_WRONLY, 0644);
    int write_return = write(fd, write_str, strlen(write_str));
    if (write_return == -1)
    {
        perror("Failed to write to file.");
        syslog(LOG_ERR, "Failed to write %s to file %s. errno: %d.", write_str, write_file, errno);
        close(fd);
        exit(1);
    }

    close(fd);

    return 0;
}