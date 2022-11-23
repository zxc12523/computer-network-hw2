#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <dirent.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <linux/tcp.h>

#include <string>
#include <vector>
#include <set>
#include <iostream>

#include "opencv2/opencv.hpp"
using namespace cv;

#define ERR_EXIT(a) \
    do              \
    {               \
        perror(a);  \
        exit(1);    \
    } while (0)

typedef struct
{
    char hostname[512];
    unsigned short port;
    int listen_fd;
} server;

typedef struct
{
    char hostname[512];
    std::string username;

    int fd;
    int conn_fd;
    int package;

    std::string comm_buf;

    char last_buf[8192];
    size_t last_buf_len;

    long long sent_bytes;
    long long max_send_bytes;
    long long remain_bytes;
    long long recv_bytes;

    bool working;

    off_t offset;

    VideoCapture cap;
    Mat img;

    int img_size;

} request;

server svr;
request *requests;
int maxfd;

struct sockaddr_in cliaddr;
int cliaddr_size;
int conn_fd;
struct timeval timeout;

fd_set rfd, working_set;

int fd;
int ret;
char buf[8192];
char init_msg[1024];
char end_msg[1024] = "sending the ending message\n";
char permission_denied_msg[1024] = "permisson denied\n\0";
char invalid_command_msg[1024] = "Commond not found\n\0";
char greeting_msg[1024] = "greeting\n\0";
char not_exist[1024] = "doesn't exist.\n\0";

char file_size[1024];
struct stat file_stat;

int remain_bytes;

std::set<std::string> banlist;

static void init_request(request *req)
{
    req->fd = -1;
    req->package = 0;
    req->last_buf_len = 0;
    req->sent_bytes = 0;
    req->max_send_bytes = 0;
    req->remain_bytes = 0;
    req->recv_bytes = -1;
    req->working = 0;
    req->offset = 0;
    req->img_size = 0;
}

static void free_request(request *reqP)
{
    init_request(reqP);
}

static void init_server(unsigned int port)
{
    struct sockaddr_in serveraddr;
    int opt = 1;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0)
    {
        ERR_EXIT("socket");
    }

    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(port);

    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        ERR_EXIT("sersockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0)
    {
        ERR_EXIT("listen");
    }

    maxfd = 1024;
    requests = (request *)malloc(sizeof(request) * maxfd);
    if (requests == NULL)
    {
        ERR_EXIT("malloc requests");
    }
    for (int i = 0; i < maxfd; i++)
    {
        init_request(&requests[i]);
    }
    requests[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requests[svr.listen_fd].hostname, svr.hostname);

    FD_ZERO(&rfd);
    FD_SET(svr.listen_fd, &rfd);

    cliaddr_size = sizeof(cliaddr);
    timeout.tv_sec = 0;
    timeout.tv_usec = 50;

    int status;
    status = mkdir("./server_dir", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    // if (status < 0)
    // {
    //     ERR_EXIT("mkdir");
    // }
    chdir("./server_dir");

    return;
}

void accept_connection()
{
    conn_fd = accept(svr.listen_fd, (struct sockaddr *)&cliaddr, (socklen_t *)&cliaddr_size);
    if (conn_fd < 0)
    {
        printf("errno: %d\n", errno);
        if (errno == EINTR || errno == EAGAIN)
        {
            fprintf(stderr, "EAGAIN\n");
        }
        else
        {
            if (errno == ENFILE)
            {
                fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
            }
            ERR_EXIT("accept");
        }
    }
    else
    {
        requests[conn_fd].conn_fd = conn_fd;
        strcpy(requests[conn_fd].hostname, inet_ntoa(cliaddr.sin_addr));
        fprintf(stderr, "accepting new connection... fd %d from %s\n", requests[conn_fd].conn_fd, requests[conn_fd].hostname);
        FD_SET(conn_fd, &rfd);
    }
}

void terminate_connection(int i)
{
    fprintf(stderr, "bad request from %s\n", requests[i].username.c_str());
    FD_CLR(i, &rfd);
    close(requests[i].conn_fd);
    free_request(&requests[i]);
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

int handle_request(request *req)
{
    int r;

    r = read(req->conn_fd, buf, 1024);
    if (r <= 0)
    {
        fprintf(stderr, "read error from fd: %d\n", req->conn_fd);
        close(req->conn_fd);
        return -1;
    }

    char *p1 = strstr(buf, "\015\012");
    if (p1 == NULL)
    {
        p1 = strstr(buf, "\012");
        if (p1 == NULL)
        {
            fprintf(stderr, "handle request error from socket: %d!\n", req->conn_fd);
            terminate_connection(req->conn_fd);
            return -1;
        }
    }
    size_t len = p1 - buf + 1;
    buf[len - 1] = '\0';
    req->comm_buf = buf;

    fprintf(stderr, "receiving request from %s\n", req->username.c_str());
    fprintf(stderr, "len: %ld, request: %s\n", req->comm_buf.size(), req->comm_buf.c_str());
    return 0;
}

void parse_request(request *req, std::vector<std::string> &commands)
{
    std::string tar = req->comm_buf;
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

void process_request(request *req)
{

    std::vector<std::string> commands;
    parse_request(req, commands);

    if (commands[0] == "greeting")
    {
        if (banlist.find(req->username) == banlist.end())
        {
            fprintf(stderr, "greeting: %s\n", req->username.c_str());
            if (send(req->conn_fd, greeting_msg, strlen(greeting_msg) + 1, MSG_NOSIGNAL) < 0)
            {
                terminate_connection(req->conn_fd);
            }
        }
        else
        {
            fprintf(stderr, "denied: %s\n", req->username.c_str());
            if (send(req->conn_fd, permission_denied_msg, strlen(permission_denied_msg) + 1, MSG_NOSIGNAL) < 0)
            {
                terminate_connection(req->conn_fd);
            }
        }
    }
    else if (commands[0] == "hello")
    {
        req->username = commands[1];

        const char *path = req->username.c_str();

        mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

        fprintf(stdout, "Accept a new connection on socket %d. Login as %s.\n", req->conn_fd, req->username.c_str());
    }
    else if (commands[0] == "ls")
    {
        const char *path = req->username.c_str();
        chdir(path);

        std::string tmp;

        struct dirent *di;
        DIR *dir = opendir(".");

        while ((di = readdir(dir)) != NULL)
        {
            tmp += di->d_name;
            tmp += '\n';
        }
        tmp += '\0';

        sprintf(init_msg, "%01023ld", tmp.size() + 1);
        if (send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL) < 0)
        {
            terminate_connection(req->conn_fd);
        }

        if (send(req->conn_fd, tmp.c_str(), tmp.size() + 1, MSG_NOSIGNAL) < 0)
        {
            terminate_connection(req->conn_fd);
        }

        closedir(dir);
        chdir("..");
    }
    else if (commands[0] == "put")
    {
        const char *path = req->username.c_str();
        chdir(path);

        if (req->remain_bytes == 0)
        {
            if ((req->fd = open(commands[1].c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644)) < 0)
            {
                ERR_EXIT("open file error");
            }

            req->recv_bytes = recv(req->conn_fd, init_msg, 1024, 0);
            req->remain_bytes = atoi(init_msg);
            req->working = 1;
            fprintf(stderr, "recv: %d file size: %d\n", req->recv_bytes, req->remain_bytes);
        }

        memcpy(&working_set, &rfd, sizeof(rfd));
        if (select(maxfd + 1, &working_set, NULL, NULL, &timeout) < 0)
        {
            ERR_EXIT("select");
        }

        if (FD_ISSET(req->conn_fd, &working_set) && (req->remain_bytes > 0) && (req->recv_bytes = recv(req->conn_fd, req->last_buf, 8192, 0)) > 0)
        {
            req->remain_bytes -= req->recv_bytes;
            // fprintf(stderr, "remain bytes: %d\n", req->remain_bytes);
            if (write(req->fd, req->last_buf, req->recv_bytes) < 0)
            {
                ERR_EXIT("write file error");
            }
        }

        if (req->remain_bytes == 0)
        {
            close(req->fd);
            init_request(req);
        }

        if (req->recv_bytes == 0)
        {
            close(req->fd);
            terminate_connection(req->conn_fd);
        }

        chdir("..");
    }
    else if (commands[0] == "get")
    {
        const char *path = req->username.c_str();
        chdir(path);

        const char *filename = commands[1].c_str();

        if (req->remain_bytes == 0)
        {
            if ((req->fd = open(filename, O_RDONLY)) < 0)
            {
                // fprintf(stderr, "file: %s doesn't exist.\n", commands[1].c_str());
                if (send(req->conn_fd, not_exist, strlen(not_exist) + 1, MSG_NOSIGNAL) < 0)
                {
                    terminate_connection(req->conn_fd);
                }
                chdir("..");
                return;
            }

            if (fstat(req->fd, &file_stat) < 0)
            {
                ERR_EXIT("fstat file error");
            }

            sprintf(init_msg, "%01023ld", file_stat.st_size);
            if (send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL) < 0)
            {
                terminate_connection(req->conn_fd);
                chdir("..");
                return;
            }

            req->offset = 0;
            req->remain_bytes = file_stat.st_size;
            req->working = 1;
            fprintf(stderr, "file size: %d\n", req->remain_bytes);
        }

        if ((req->remain_bytes > 0) && (req->last_buf_len = read(req->fd, req->last_buf, 1024)) > 0 && (req->sent_bytes = send(req->conn_fd, req->last_buf, req->last_buf_len, MSG_NOSIGNAL)) > 0)
        {
            req->remain_bytes -= req->sent_bytes;
            // fprintf(stderr, "remaining file size: %d\n", req->remain_bytes);
        }

        if (req->remain_bytes == 0)
        {
            close(req->fd);
            init_request(req);
        }

        if (req->sent_bytes < 0)
        {
            close(req->fd);
            terminate_connection(req->conn_fd);
        }

        chdir("..");
    }
    else if (commands[0] == "play")
    {
        const char *path = req->username.c_str();
        chdir(path);

        const char *video_name = commands[1].c_str();

        VideoCapture cap(video_name);

        if (req->remain_bytes == 0)
        {
            if (access(video_name, F_OK) != 0)
            {
                if (send(req->conn_fd, not_exist, strlen(not_exist) + 1, MSG_NOSIGNAL) < 0)
                {
                    terminate_connection(req->conn_fd);
                }

                cap.release();
                chdir("..");
                return;
            }

            int width = cap.get(CAP_PROP_FRAME_WIDTH);
            int height = cap.get(CAP_PROP_FRAME_HEIGHT);
            int frame_num = cap.get(CAP_PROP_FRAME_COUNT);

            req->img = Mat::zeros(height, width, CV_8UC3);

            if (!req->img.isContinuous())
            {
                req->img = req->img.clone();
            }

            int imgSize = req->img.total() * req->img.elemSize();
            sprintf(init_msg, "%200ld%200ld%200ld%200ld%200ld%23d", width, height, imgSize, 0, frame_num, 0);
            if (send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL) < 0)
            {
                terminate_connection(req->conn_fd);
                return;
            }

            cap >> req->img;

            req->working = 1;
            req->cap = cap;
            req->remain_bytes = frame_num * imgSize;
            req->img_size = imgSize;
            req->max_send_bytes = imgSize;
        }
        else
        {
            cap = req->cap;
        }

        memcpy(&working_set, &rfd, sizeof(rfd));
        if (select(maxfd + 1, &working_set, NULL, NULL, &timeout) < 0)
        {
            ERR_EXIT("select");
        }

        if (FD_ISSET(req->conn_fd, &working_set) && (req->recv_bytes = recv(req->conn_fd, req->last_buf, 1024, 0)) > 0)
        {
            if (strcmp(req->last_buf, "send video package.\n") == 0)
            {
                // fprintf(stderr, "sending video pakcage %d\n", req->package++);

                req->sent_bytes = send(req->conn_fd, req->img.data + req->offset,
                                       min(req->max_send_bytes, (long long)(req->img_size - req->offset)), MSG_NOSIGNAL);

                if (req->sent_bytes < 0)
                {
                    terminate_connection(req->conn_fd);
                    return;
                }

                req->offset += req->sent_bytes;
                req->remain_bytes -= req->sent_bytes;

                if (req->offset == req->img_size)
                {
                    req->offset = 0;
                    cap >> req->img;
                }

                if (req->remain_bytes == 0)
                {
                    req->cap.release();
                    init_request(req);
                }
            }
            else if (strcmp(req->last_buf, "terminate video.\n") == 0)
            {
                fprintf(stderr, "terminating video\n");
                req->cap.release();
                init_request(req);
            }
        }

        if (req->recv_bytes == 0)
        {
            terminate_connection(req->conn_fd);
        }

        chdir("..");
    }
    else if (commands[0] == "ban")
    {
        std::string tmp;

        for (int i = 1; i < commands.size(); i++)
        {
            if (commands[i] == "admin")
                tmp += "You cannot ban yourself!\n";
            else if (banlist.find(commands[i]) == banlist.end())
                tmp += "Ban " + commands[i] + " Successfully!\n";
            else
                tmp += "User " + commands[i] + " is already on the blocklist!\n";
        }
        tmp += '\0';

        sprintf(init_msg, "%01023ld", req->username != "admin" ? strlen(permission_denied_msg) + 1 : tmp.size() + 1);
        req->sent_bytes = send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL);

        if (req->username != "admin")
        {
            req->sent_bytes = send(req->conn_fd, permission_denied_msg, strlen(permission_denied_msg) + 1, MSG_NOSIGNAL);
        }
        else
        {
            req->sent_bytes = send(req->conn_fd, tmp.c_str(), tmp.size() + 1, MSG_NOSIGNAL);
            for (int i = 1; i < commands.size(); i++)
                if (commands[i] != "admin")
                    banlist.insert(commands[i]);
        }

        if (req->sent_bytes < 0)
        {
            terminate_connection(req->conn_fd);
        }
        else
        {
            init_request(req);
        }
    }
    else if (commands[0] == "unban")
    {
        std::string tmp;

        for (int i = 1; i < commands.size(); i++)
        {
            if (banlist.find(commands[i]) == banlist.end())
                tmp += "User " + commands[i] + " is not on the blocklist!\n";
            else
                tmp += "Successfully removed " + commands[i] + " from the blocklist!\n";
        }
        tmp += '\0';

        sprintf(init_msg, "%01023ld", req->username != "admin" ? strlen(permission_denied_msg) + 1 : tmp.size() + 1);
        req->sent_bytes = send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL);

        if (req->username != "admin")
        {
            req->sent_bytes = send(req->conn_fd, permission_denied_msg, strlen(permission_denied_msg) + 1, MSG_NOSIGNAL);
        }
        else
        {
            req->sent_bytes = send(req->conn_fd, tmp.c_str(), tmp.size() + 1, MSG_NOSIGNAL);
            for (int i = 1; i < commands.size(); i++)
                banlist.erase(commands[i]);
        }

        if (req->sent_bytes < 0)
        {
            terminate_connection(req->conn_fd);
        }
        else
        {
            init_request(req);
        }
    }
    else if (commands[0] == "blocklist")
    {
        std::string tmp;

        for (auto i : banlist)
        {
            tmp += i + '\n';
        }
        tmp += '\0';

        sprintf(init_msg, "%01023ld", req->username != "admin" ? strlen(permission_denied_msg) + 1 : tmp.size() + 1);
        req->sent_bytes = send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL);

        if (req->username != "admin")
            req->sent_bytes = send(req->conn_fd, permission_denied_msg, strlen(permission_denied_msg) + 1, MSG_NOSIGNAL);
        else
            req->sent_bytes = send(req->conn_fd, tmp.c_str(), tmp.size() + 1, MSG_NOSIGNAL);

        if (req->sent_bytes < 0)
        {
            terminate_connection(req->conn_fd);
        }
        else
        {
            init_request(req);
        }
    }
    else
    {
        sprintf(init_msg, "%01023ld", strlen(invalid_command_msg) + 1);
        req->sent_bytes = send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL);
        req->sent_bytes = send(req->conn_fd, invalid_command_msg, strlen(invalid_command_msg) + 1, MSG_NOSIGNAL);

        if (req->sent_bytes < 0)
        {
            terminate_connection(req->conn_fd);
        }
        else
        {
            init_request(req);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    init_server((unsigned int)atoi(argv[1]));

    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    int time = 0;

    while (1)
    {
        memcpy(&working_set, &rfd, sizeof(rfd));

        if (select(maxfd + 1, &working_set, NULL, NULL, &timeout) < 0)
        {
            ERR_EXIT("select");
        }

        for (int i = 0; i < maxfd; i++)
        {
            if (FD_ISSET(i, &working_set) || requests[i].working)
            {
                // fprintf(stderr, "working_set fd: %d ready\n", i);
                if (i == svr.listen_fd)
                {
                    accept_connection();
                }
                else if (requests[i].working)
                {
                    process_request(&requests[i]);
                }
                else if ((ret = handle_request(&requests[i])) < 0)
                {
                    terminate_connection(i);
                }
                else
                {
                    process_request(&requests[i]);
                }
            }
        }
    }
}