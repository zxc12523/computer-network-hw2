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
#include <sys/sendfile.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <string>
#include <vector>
#include <iostream>

// #include "opencv2/opencv.hpp"
// using namespace cv;

#define ERR_EXIT(a) \
    do              \
    {               \
        perror(a);  \
        exit(1);    \
    } while (0)

int sockfd, clientfd;
int fd;
char init_msg[1024];
char end_msg[1024] = "sending the ending message\n";
char command[1024], buf[1024];
char username[512], ip[512], port[512];

fd_set rfd, working_set;

struct timeval timeout;

int sent_bytes = 0;
int recv_bytes = 0;

char file_size[256];
struct stat file_stat;

off_t offset;
int remain_bytes;

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

    // FL_SET(sockfd, O_NONBLOCK);

    mkdir("./client_database", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    chdir("./client_database");

    sprintf(buf, "hello %s\n", username);
    send(sockfd, buf, 1024, 0);

    FD_ZERO(&rfd);
    FD_SET(sockfd, &rfd);

    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    return 0;
}

void FL_SET(int fd, int flag)
{
    int val;
    if (val = fcntl(fd, F_GETFL, 0) < 0)
        fprintf(stderr, "get flag err\n");

    val |= flag;

    if (fcntl(fd, F_SETFL, val) < 0)
        fprintf(stderr, "set flag err\n");
}

void parse_command(char *command, std::vector<std::string> &commands)
{
    command[strlen(command) - 1] = '\0';
    std::string tar = std::string(command);
    std::string delimiter = " ";

    std::string buf;
    size_t pos = 0;

    while ((pos = tar.find(delimiter)) != std::string::npos)
    {
        buf = tar.substr(0, pos);
        if (buf.size())
            commands.push_back(buf);
        tar.erase(0, pos + delimiter.length());
    }
    commands.push_back(tar);
}

void process_command()
{
    std::vector<std::string> commands;
    parse_command(command, commands);
    command[strlen(command)] = '\n';

    if (commands[0] == "put")
    {
        const char *filename = commands[1].c_str();

        if ((fd = open(filename, O_RDONLY)) < 0)
        {
            fprintf(stderr, "%s doesn't exist.\n", filename);
            return;
        }

        if (fstat(fd, &file_stat) < 0)
        {
            ERR_EXIT("open file error");
        }

        fprintf(stderr, "putting %s...\n", filename);

        send(sockfd, command, 1024, 0);
        sprintf(init_msg, "%01023ld", file_stat.st_size);
        send(sockfd, init_msg, 1024, 0);

        offset = 0;
        remain_bytes = file_stat.st_size;

        // fprintf(stderr, "file size: %ld\n", remain_bytes);

        while ((remain_bytes > 0) && ((sent_bytes = sendfile(sockfd, fd, &offset, BUFSIZ)) > 0))
        {
            // fprintf(stderr, "sent bytes: %ld bytes\n", sent_bytes);
            remain_bytes -= sent_bytes;
            // fprintf(stderr, "remaining file size: %ld\n", remain_bytes);
        }

        close(fd);
    }
    else if (commands[0] == "get")
    {
        send(sockfd, command, 1024, 0);
        recv(sockfd, init_msg, 1024, 0);

        if (strcmp(init_msg, "doesn't exist.\n") == 0)
        {
            fprintf(stderr, "%s doesn't exist.\n", commands[1].c_str());
            return;
        }

        fprintf(stderr, "getting %s...\n", commands[1].c_str());

        if ((fd = open(commands[1].c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644)) < 0)
        {
            ERR_EXIT("open file error");
        }

        offset = 0;
        remain_bytes = atoi(init_msg);

        // fprintf(stderr, "file size: %ld\n", remain_bytes);

        while ((remain_bytes > 0) && ((recv_bytes = recv(sockfd, buf, 1024, 0)) > 0))
        {
            // fprintf(stderr, "received bytes: %ld bytes\n", recv_bytes);
            remain_bytes -= recv_bytes;
            // fprintf(stderr, "remaining file size: %ld\n", remain_bytes);
            if (write(fd, buf, recv_bytes) < 0)
            {
                ERR_EXIT("write file error");
            }
        }

        close(fd);
    }
    else if (commands[0] == "play")
    {
        // Mat client_img;
        // int width, height, imgSize, maxBytes, frame_num;

        // send(sockfd, command, 1024, 0);
        // recv(sockfd, init_msg, 1024, 0);

        // if (strcmp(init_msg, "doesn't exist.\n") == 0)
        // {
        //     fprintf(stderr, "%s doesn't exist.\n", commands[1].c_str());
        //     return;
        // }

        // fprintf(stderr, "playing the video...\n");

        // width = atoi(init_msg);
        // height = atoi(init_msg + 200);
        // imgSize = atoi(init_msg + 400);
        // maxBytes = atoi(init_msg + 600);
        // frame_num = atoi(init_msg + 800);

        // remain_bytes = frame_num * imgSize;

        // client_img = Mat::zeros(height, width, CV_8UC3);

        // if (!client_img.isContinuous())
        // {
        //     client_img = client_img.clone();
        // }

        // uchar *iptr = client_img.data;
        // char buffer[maxBytes];

        // int flag = 1;
        // int copy_from = 0;

        // while (flag && (remain_bytes > 0) && ((recv_bytes = recv(sockfd, buffer + copy_from, maxBytes - copy_from, 0)) > 0))
        // {
        //     copy_from += recv_bytes;
        //     remain_bytes -= recv_bytes;

        //     if (copy_from == maxBytes)
        //     {
        //         copy_from = 0;
        //         for (int i = 0; i < maxBytes / imgSize; i++)
        //         {
        //             memcpy(iptr, buffer + copy_from, imgSize);
        //             imshow("Video", client_img);
        //             copy_from += imgSize;
        //             char c = (char)waitKey(33.3333);
        //             if (c == 27)
        //             {
        //                 flag = 0;
        //                 break;
        //             }
        //         }
        //         copy_from = 0;
        //     }
        // }

        // destroyAllWindows();

        // while ((remain_bytes > 0) && ((recv_bytes = recv(sockfd, buffer, maxBytes, 0)) > 0))
        // {
        //     remain_bytes -= recv_bytes;
        // }
    }
    else
    {
        send(sockfd, command, 1024, 0);
        recv(sockfd, init_msg, 1024, 0);

        offset = 0;
        remain_bytes = atoi(init_msg);

        while ((remain_bytes > 0) && ((recv_bytes = recv(sockfd, buf, 1024, 0)) > 0))
        {
            // fprintf(stderr, "received bytes: %ld bytes\n", recv_bytes);
            remain_bytes -= recv_bytes;
            // fprintf(stderr, "remaining file size: %ld\n", remain_bytes);
            fprintf(stderr, "%s", buf);
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

        sprintf(init_msg, "greeting\n");

        send(sockfd, init_msg, 1024, MSG_NOSIGNAL);
        recv(sockfd, init_msg, 1024, 0);

        if (strcmp(init_msg, "greeting\n") == 0)
        {
            process_command();
        }
        else
        {
            fprintf(stderr, "%s", init_msg);
        }

        memset(command, 0, sizeof(command));
    }

    close(clientfd);
}