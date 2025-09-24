#include <stdio.h>
#include <syslog.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    openlog(NULL, 0, LOG_USER);
    if (argc < 3)
    {
        syslog(LOG_ERR, "usage: %s <filepath> <string to write>", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_CREAT | O_WRONLY, 0666);
    if (fd < 0 )
    {
        syslog(LOG_ERR, "Error while opening %s", argv[1]);
    }
    else
    {
        syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
        write(fd, argv[2], strlen(argv[2]));
    }

    closelog();
    return 0;
}