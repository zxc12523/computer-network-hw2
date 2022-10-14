#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
// #include <sys/sendfile.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <string>
#include <vector>
#include <iostream>

#define ERR_EXIT(a) \
    do              \
    {               \
        perror(a);  \
        exit(1);    \
    } while (0)

int sockfd, clientfd;
char username[512], ip[512], port[512];
char command[1024], tmp[1024];

fd_set rfd, working_set;

struct timeval timeout;

int sent_bytes = 0;
int recv_bytes = 0;

char file_size[256];
struct stat file_stat;

int offset;
int remain_bytes;

void FL_SET(int fd, int flag)
{
    int val;
    if (val = fcntl(fd, F_GETFL, 0) < 0)
        fprintf(stderr, "get flag err\n");

    val |= flag;

    if (fcntl(fd, F_SETFL, val) < 0)
        fprintf(stderr, "set flag err\n");
}

int init(int argc, char **argv)
{
    strncpy(username, argv[1], strlen(argv[1]));

    int l = strlen(argv[2]);

    for (int i = 0; i < l; i++)
    {
        if (argv[2][i] == ':')
        {
            strncpy(ip, argv[2], i);
            strncpy(port, argv[2] + i + 1, l - i - 1);
            break;
        }
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        ERR_EXIT("socket");
    }

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(atoi(port));
    if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0)
    {
        ERR_EXIT("Invalid address/ Address not supported \n");
    }

    if ((clientfd = connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) < 0)
    {
        ERR_EXIT("connect");
    }

    FL_SET(sockfd, O_NONBLOCK);

    mkdir("./client_database", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    chdir("./client_database");

    sprintf(tmp, "hello %s\n", username);
    write(sockfd, tmp, 1024);

    FD_ZERO(&rfd);
    FD_SET(sockfd, &rfd);

    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    return 0;
}

void parse_command(char *command, std::vector<std::string> &commands)
{
    command[strlen(command) - 1] = '\0';
    std::string tar = std::string(command);
    std::string delimiter = " ";

    std::string tmp;
    size_t pos = 0;

    while ((pos = tar.find(delimiter)) != std::string::npos)
    {
        tmp = tar.substr(0, pos);
        if (tmp.size())
            commands.push_back(tmp);
        tar.erase(0, pos + delimiter.length());
    }
    commands.push_back(tar);
}

void process_command(std::vector<std::string> commands)
{
    memcpy(&working_set, &rfd, sizeof(rfd));

    if (select(1024 + 1, &working_set, NULL, NULL, &timeout) < 0)
    {
        ERR_EXIT("select");
    }

    if (commands[0] == "ls")
    {
        if (FD_ISSET(sockfd, &working_set))
        {
            while (read(sockfd, tmp, 1024) != -1)
            {
                fprintf(stderr, "%s\n", tmp);
            }
        }
    }
    else if (commands[0] == "put")
    {
        char filename[1024], init_msg[1024];
        sprintf(filename, "./%s", commands[1]);
        int fd = open(filename, O_RDONLY);

        if (fstat(fd, &file_stat) < 0) {
            ERR_EXIT("open file error");
        }

        sprintf(init_msg, "sending %d char.", file_stat.st_size);

        if (send(sockfd, init_msg, 1024, 0) < 0) {
            ERR_EXIT("sending greeting error");
        }

        offset = 0;
        remain_bytes = file_stat.st_size;

        while(((sent_bytes = sendfile(sockfd, fd, &offset, BUFSIZ)) > 0 && (remain_bytes > 0))) {
            remain_bytes -= sent_bytes;
        }
    }
    else if (commands[0] == "get")
    {
        char filename[1024];
        sprintf(filename, "./%s", commands[1]);
        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);

        
        if (recv(sockfd, file_size, 1024, 0) < 0) {
            ERR_EXIT("receiving greeting error");
        }

        offset = 0;
        remain_bytes = atoi(file_size);

        while(((recv_bytes = recv(sockfd, tmp, 1024, 0)) > 0 && (remain_bytes > 0))) {
            remain_bytes -= sent_bytes;
            write(fd, tmp, 1024);
        }

        close(fd);
    }
    else if (commands[0] == "play")
    {
        
    }
    else if (commands[0] == "ban")
    {
        if (FD_ISSET(sockfd, &working_set))
        {
            while (read(sockfd, tmp, 1024) != -1)
            {
                fprintf(stderr, "%s\n", tmp);
            }
        }
    }
    else if (commands[0] == "unban")
    {
        if (FD_ISSET(sockfd, &working_set))
        {
            while (read(sockfd, tmp, 1024) != -1)
            {
                fprintf(stderr, "%s\n", tmp);
            }
        }
    }
    else if (commands[0] == "blacklist")
    {
        if (FD_ISSET(sockfd, &working_set))
        {
            while (read(sockfd, tmp, 1024) != -1)
            {
                fprintf(stderr, "%s\n", tmp);
            }
        }
    }
    else
    {
        if (FD_ISSET(sockfd, &working_set))
        {
            while (read(sockfd, tmp, 1024) != -1)
            {
                fprintf(stderr, "%s\n", tmp);
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: ./client [username] [ip]:[port]\n");
        exit(1);
    }

    init(argc, argv);

    fprintf(stderr, "username: %s ip: %s port: %s\n", username, ip, port);

    while (1)
    {
        fprintf(stderr, "$ ");

        read(STDIN_FILENO, command, 1024);
        write(sockfd, command, 1024);

        std::vector<std::string> commands;
        parse_command(command, commands);

        if (fork() == 0)
        {
            process_command(commands);
            exit(0);
        }
        else
        {
            wait(NULL);
        }

        memset(command, 0, sizeof(command));
    }

    close(clientfd);
}