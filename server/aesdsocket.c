#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>

#define PORT "9000" // Port to listen on

int sockfd = -1, new_fd = -1;
int running = 0;

void cleanup()
{
    if (new_fd != -1)
        close(new_fd);
    if (sockfd != -1)
        close(sockfd);
    running = 0;
}

void signalhandler(int signo)
{
    printf("Caught signal, exiting\n");
    syslog(LOG_INFO, "Caught signal, exiting");
    cleanup();
}

void sendreply(int fd, int new_fd)
{
    off_t fsize = lseek(fd, 0, SEEK_CUR);
    lseek(fd, 0, SEEK_SET);

    uint8_t *data = malloc(fsize);
    if (data == NULL)
    {
        perror("malloc");
        return;
    }

    read(fd, data, fsize);

    size_t sent = send(new_fd, data, fsize, 0);
    if (sent != fsize)
    {
        perror("send");
    }
    else
    {
        printf("sending: ");
        for (int i = 0; i < fsize; i++)
            printf("%c", data[i]);
    }

    free(data);
}

int main(int argc, const char **argv)
{
    struct addrinfo hints, *res;
    struct sockaddr_in their_addr;
    socklen_t addr_size;
    char buf[1024];
    int bytes_received;
    char client_ip[INET6_ADDRSTRLEN];

    int run_as_daemon = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0)
    {
        run_as_daemon = 1;
        printf("demon mode requested\n");
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGINT, signalhandler);
    signal(SIGTERM, signalhandler);

    // Prepare hints
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // Use my IP

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0)
    {
        perror("getaddrinfo");
        return -1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == -1)
    {
        perror("socket");
        return -1;
    }

    if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1)
    {
        perror("bind");
        cleanup();
        return -1;
    }

    freeaddrinfo(res);

    if (listen(sockfd, 10) == -1)
    {
        perror("listen");
        cleanup();
        return -1;
    }

    system("mkdir -p /var/tmp/");
    int fd = open("/var/tmp/aesdsocketdata", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd < 0)
    {
        perror("failed to open file!");
        cleanup();
        return -1;
    }

    if (run_as_daemon)
    {
        pid_t pid = fork();
        if (pid > 0) // parent
        {
            printf("exiting parent\n");
            exit(0);
        }
    }

    running = 1;
    while (running)
    {

        printf("Server: waiting for connections...\n");

        addr_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
        if (new_fd == -1)
        {
            perror("accept");
        }
        else
        {
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);

            open("/dev/null", O_RDONLY); // stdin
            open("/dev/null", O_WRONLY); // stdout
            open("/dev/null", O_RDWR);   // stderr

            // Convert client IP to string
            if (inet_ntop(AF_INET, &their_addr.sin_addr, client_ip, sizeof(client_ip)) == NULL)
            {
                perror("inet_ntop");
                continue;
            }

            printf("Accepted connection from %s\n", client_ip);
            syslog(LOG_INFO, "Accepted connection from %s", client_ip);

            while (running)
            {
                bytes_received = recv(new_fd, buf, sizeof buf, 0);
                if (bytes_received > 0)
                {
                    printf("\nServer received[%d]:", bytes_received);
                    write(fd, buf, bytes_received);

                    for (int i = 0; i < bytes_received; i++)
                    {
                        printf("%c", buf[i]);
                        if ('\n' == buf[i])
                        {
                            sendreply(fd, new_fd);
                            // break;
                        }
                    }
                }
                else
                {
                    break;
                }
            }

            printf("Closed connection from %s\n", client_ip);
            syslog(LOG_INFO, "Closed connection from %s", client_ip);
        }
    }

    close(fd);
    cleanup();
    return 0;
}
