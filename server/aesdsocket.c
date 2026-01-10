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
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include "aesd_ioctl.h"

#define PORT "9000" // Port to listen on
#define ERROR (-1)

int running = 0;
int servfd = ERROR;
int logfd = ERROR;
pthread_mutex_t log_mtx;

struct ConnInfo
{
    struct sockaddr_in their_addr;
    int recvfd;
};

struct Node
{
    struct Node *next;
    struct ConnInfo conn;
    pthread_t thread;
};

int getdev()
{
#if USE_AESD_CHAR_DEVICE
    if (logfd == ERROR)
    {
        logfd = open("/dev/aesdchar", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (logfd < 0)
        {
            perror("failed to open file!");
            exit(ERROR);
        }
    }
#endif
    return logfd;
}

void writelog(const char *buf, size_t len)
{
    pthread_mutex_lock(&log_mtx);
    write(getdev(), buf, len);
    pthread_mutex_unlock(&log_mtx);
}

void readlog(char *buf, size_t len)
{
    pthread_mutex_lock(&log_mtx);
    read(getdev(), buf, len);
    pthread_mutex_unlock(&log_mtx);
}

void signalhandler(int signo)
{
    printf("Caught signal, exiting\n");
    syslog(LOG_INFO, "Caught signal, exiting");

    if (servfd != ERROR)
        close(servfd);
    running = 0;
}

void timeouthandler(int signo)
{
    time_t now;
    struct tm *local_info;
    const char *format_string = "timestamp:%a, %d %b %Y %H:%M:%S %z\n";
    time(&now);
    local_info = localtime(&now);
    char timestamp[200];
    strftime(timestamp, sizeof(timestamp), format_string, local_info);
    printf("%s\n", timestamp);
    writelog(timestamp, strlen(timestamp));
    alarm(10);
}

void sendreply(int recvfd, const struct aesd_seekto *seekto)
{
    if (logfd == ERROR)
        return;

    pthread_mutex_lock(&log_mtx);
    off_t fsize = lseek(logfd, 0, SEEK_END);
    lseek(logfd, 0, SEEK_SET);
    pthread_mutex_unlock(&log_mtx);

    uint8_t *data = malloc(fsize);
    if (data == NULL)
    {
        perror("malloc");
        return;
    }

    if (seekto->write_cmd)
    {
        ioctl(logfd, AESDCHAR_IOCSEEKTO, seekto);
    }

    readlog(data, fsize);

    size_t sent = send(recvfd, data, fsize, 0);
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

void *handle(void *arg)
{

    if (arg == NULL)
        return NULL;

    struct ConnInfo *info = (struct ConnInfo *)arg;
    int recvfd = info->recvfd;
    struct sockaddr_in their_addr = info->their_addr;
    int bytes_received;
    char client_ip[INET6_ADDRSTRLEN];
    char buf[1024];

    // Convert client IP to string
    if (inet_ntop(AF_INET, &their_addr.sin_addr, client_ip, sizeof(client_ip)) == NULL)
    {
        perror("inet_ntop");
        return NULL;
    }

    printf("Accepted connection from %s\n", client_ip);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    while (running)
    {
        bytes_received = recv(recvfd, buf, sizeof buf, 0);
        if (bytes_received > 0)
        {
            bool completed = false;
            printf("\nServer received[%d]: ", bytes_received);
            for (int i = 0; i < bytes_received; i++)
            {
                printf("%c", buf[i]);
                if ('\n' == buf[i])
                {
                    completed = true;
                }
            }

            static const char ioctl_cmd[] = "AESDCHAR_IOCSEEKTO:";
            static const size_t ioctl_cmd_len = sizeof(ioctl_cmd) - 1u;
            struct aesd_seekto seekto = {.write_cmd = 0, .write_cmd_offset = 0};

            if ((bytes_received > ioctl_cmd_len) &&
                (memcmp(ioctl_cmd, buf, ioctl_cmd_len) == 0))
            {
                sscanf(buf, "AESDCHAR_IOCSEEKTO:%u,%u", &seekto.write_cmd, &seekto.write_cmd_offset);
                printf("got ioctl seek command - write_cmd %u write_cmd_offset %u\n", seekto.write_cmd, seekto.write_cmd_offset);
            }
            else
            {
                writelog(buf, bytes_received);
            }

            if (completed)
            {
                sendreply(recvfd, &seekto);
            }
        }
        else
        {
            break;
        }
    }

    printf("Closed connection from %s\n", client_ip);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    close(recvfd);
    close(logfd);
    logfd = ERROR;
    return NULL;
}

int main(int argc, const char **argv)
{
    struct addrinfo hints, *res;
    struct sockaddr_in their_addr;
    socklen_t addr_size;

    int run_as_daemon = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0)
    {
        run_as_daemon = 1;
        printf("demon mode requested\n");
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGINT, signalhandler);
    signal(SIGTERM, signalhandler);

    if (pthread_mutex_init(&log_mtx, NULL) != 0)
    {
        perror("mutex");
        return ERROR;
    }

    // Prepare hints
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // Use my IP

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0)
    {
        perror("getaddrinfo");
        return ERROR;
    }

    servfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (servfd == ERROR)
    {
        perror("socket");
        return ERROR;
    }

    if (bind(servfd, res->ai_addr, res->ai_addrlen) == ERROR)
    {
        perror("bind");
        close(servfd);
        return ERROR;
    }

    freeaddrinfo(res);

    if (listen(servfd, 10) == ERROR)
    {
        perror("listen");
        close(servfd);
        return ERROR;
    }

#if USE_AESD_CHAR_DEVICE == 0
    system("mkdir -p /var/tmp/");
    logfd = open("/var/tmp/aesdsocketdata", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (logfd < 0)
    {
        perror("failed to open file!");
        close(servfd);
        return ERROR;
    }
#endif

    if (run_as_daemon)
    {
        pid_t pid = fork();
        if (pid > 0) // parent
        {
            printf("exiting parent\n");
            exit(0);
        }
        else
        {
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);

            open("/dev/null", O_RDONLY); // stdin
            open("/dev/null", O_WRONLY); // stdout
            open("/dev/null", O_RDWR);   // stderr
        }
    }

    printf("pid : %d\n", getpid());

#if USE_AESD_CHAR_DEVICE == 0
    alarm(10);
    signal(SIGALRM, timeouthandler);
#endif

    struct Node *head = NULL;

    running = 1;
    while (running)
    {
        printf("Server: waiting for connections...\n");

        addr_size = sizeof their_addr;
        int recvfd = accept(servfd, (struct sockaddr *)&their_addr, &addr_size);
        if (recvfd == ERROR)
        {
            perror("accept");
        }
        else
        {
            struct Node *node = malloc(sizeof(struct Node));
            node->next = head;
            node->conn.recvfd = recvfd;
            node->conn.their_addr = their_addr;
            pthread_create(&node->thread, NULL, handle, &node->conn);
            head = node;
        }
    }

    while (head != NULL)
    {
        pthread_join(head->thread, NULL);
        struct Node *next = head->next;
        free(head);
        head = next;
    }

    close(servfd);
#if USE_AESD_CHAR_DEVICE == 0
    fsync(logfd);
    close(logfd);
#endif
    pthread_mutex_destroy(&log_mtx);

    return 0;
}
