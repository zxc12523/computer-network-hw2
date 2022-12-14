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

#include <string>
#include <vector>
#include <set>
#include <iostream>

// #include "opencv2/opencv.hpp"
// using namespace cv;

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

    int conn_fd;

    char buf[1024];
    size_t buf_len;

    char last_buf[1024];
    size_t last_buf_len;

    long long sended_bytes;
    long long max_send_bytes;
    long long remain_bytes;

    // VideoCapture cap;

} request;

server svr;
request *requests;
int maxfd;

struct sockaddr_in cliaddr;
int cliaddr_size;
int conn_fd;
struct timeval timeout;

fd_set rfd, working_set;

int sent_bytes = 0;
int recv_bytes = 0;

int fd;
int ret;
char init_msg[1024];
char end_msg[1024] = "sending the ending message\n";
char permission_denied_msg[1024] = "permisson denied\n\0";
char invalid_command_msg[1024] = "invalid commond\n\0";
char greeting_msg[1024] = "greeting\n\0";

char file_size[1024];
struct stat file_stat;

off_t offset;
int remain_bytes;

std::set<std::string> banlist;

static void init_request(request *req)
{
    req->conn_fd = -1;
    req->buf_len = 0;
    req->last_buf_len = 0;
    req->sended_bytes = 0;
    req->max_send_bytes = 0;
    req->remain_bytes = 0;
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

    int status;
    status = mkdir("./server_database", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    // if (status < 0)
    // {
    //     ERR_EXIT("mkdir");
    // }
    chdir("./server_database");

    return;
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
    char buf[1024];

    r = read(req->conn_fd, buf, sizeof(buf));
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
            ERR_EXIT("this really should not happen...");
        }
    }
    size_t len = p1 - buf + 1;
    memmove(req->buf, buf, len);
    req->buf[len - 1] = '\0';
    req->buf_len = len - 1;

    fprintf(stderr, "receiving request from %s\n", req->hostname);
    fprintf(stderr, "len: %ld, request: %s\n", req->buf_len, req->buf);
    return 0;
}

void parse_request(request *req, std::vector<std::string> &commands)
{
    std::string tar = req->buf;
    std::string delimiter = " ";

    std::string tmp;
    size_t pos = 0;

    while ((pos = tar.find(delimiter)) != std::string::npos)
    {
        tmp = tar.substr(0, pos);
        commands.push_back(tmp);
        tar.erase(0, pos + delimiter.length());
    }
    commands.push_back(tar);
}

void process_request(request *req, std::vector<std::string> &commands)
{
    if (commands[0] == "greeting")
    {
        if (banlist.find(req->username) == banlist.end())
        {
            fprintf(stderr, "greeting: %s\n", req->username.c_str());
            fprintf(stderr, "len: %d msg: %s", strlen(greeting_msg) + 1, greeting_msg);
            send(req->conn_fd, greeting_msg, strlen(greeting_msg) + 1, MSG_NOSIGNAL);
        }
        else
        {
            fprintf(stderr, "denied: %s\n", req->username.c_str());
            send(req->conn_fd, permission_denied_msg, strlen(permission_denied_msg) + 1, MSG_NOSIGNAL);
        }
    }
    else if (commands[0] == "hello")
    {
        req->username = commands[1];

        const char *path = req->username.c_str();

        mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }
    else if (commands[0] == "ls")
    {
        const char *path = req->username.c_str();
        chdir(path);

        std::string tmp;

        struct dirent *di;
        DIR *dir = opendir(".");

        if (dir == NULL) // opendir returns NULL if couldn't open directory
        {
            write(req->conn_fd, "ERROR: Couldn't open directory", 1024);
            ERR_EXIT("Couldn't open directory");
        }

        while ((di = readdir(dir)) != NULL)
        {
            tmp += di->d_name;
            tmp += '\n';
        }
        tmp += '\0';

        sprintf(init_msg, "%01023ld", tmp.size() + 1);
        send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL);
        send(req->conn_fd, tmp.c_str(), tmp.size() + 1, MSG_NOSIGNAL);

        closedir(dir);
        chdir("..");
    }
    else if (commands[0] == "put")
    {
        const char *path = req->username.c_str();
        chdir(path);

        if ((fd = open(commands[1].c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644)) < 0)
        {
            ERR_EXIT("open file error");
        }

        recv(req->conn_fd, init_msg, 1024, 0);

        offset = 0;
        remain_bytes = atoi(init_msg);

        fprintf(stderr, "file size: %d\n", remain_bytes);

        while ((remain_bytes > 0) && ((recv_bytes = recv(req->conn_fd, req->buf, 1024, 0)) > 0))
        {
            // fprintf(stderr, "received bytes: %d bytes\n", remain_bytes);
            remain_bytes -= recv_bytes;
            // fprintf(stderr, "remaining file size: %d\n", remain_bytes);
            if (write(fd, req->buf, recv_bytes) < 0)
            {
                ERR_EXIT("write file error");
            }
        }

        close(fd);
        chdir("..");
    }
    else if (commands[0] == "get")
    {
        const char *path = req->username.c_str();
        chdir(path);

        const char *filename = commands[1].c_str();

        if ((fd = open(filename, O_RDONLY)) < 0)
        {
            ERR_EXIT("open");
        }

        if (fstat(fd, &file_stat) < 0)
        {
            ERR_EXIT("open file error");
        }

        sprintf(init_msg, "%01023ld", file_stat.st_size);
        send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL);

        offset = 0;
        remain_bytes = file_stat.st_size;

        fprintf(stderr, "file size: %d\n", remain_bytes);

        while ((remain_bytes > 0) && ((sent_bytes = sendfile(req->conn_fd, fd, &offset, BUFSIZ)) > 0))
        {
            fprintf(stderr, "sent bytes: %d bytes\n", sent_bytes);
            remain_bytes -= sent_bytes;
            fprintf(stderr, "remaining file size: %d\n", remain_bytes);
        }

        close(fd);
        chdir("..");
    }
    else if (commands[0] == "play")
    {
        // const char *path = req->username.c_str();
        // chdir(path);

        // const char *video_name = commands[1].c_str();

        // VideoCapture cap(video_name);

        // if (req->remain_bytes)
        // {
        //     cap = req->cap;
        // }

        // int width = cap.get(CAP_PROP_FRAME_WIDTH);
        // int height = cap.get(CAP_PROP_FRAME_HEIGHT);
        // int frame_num = cap.get(CAP_PROP_FRAME_COUNT);

        // Mat server_img;
        // server_img = Mat::zeros(height, width, CV_8UC3);

        // if (!server_img.isContinuous())
        // {
        //     server_img = server_img.clone();
        // }

        // int imgSize = server_img.total() * server_img.elemSize();

        // req->sended_bytes = 0;
        // req->max_send_bytes = 1 * imgSize;

        // if (req->remain_bytes == 0)
        // {
        //     req->remain_bytes = frame_num * imgSize;
        //     fprintf(stderr, "%d %d %d %d %d\n", width, height, imgSize, req->max_send_bytes, frame_num);
        //     sprintf(init_msg, "%200ld%200ld%200ld%200ld%200ld%23d", width, height, imgSize, req->max_send_bytes, frame_num, 0);
        //     send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL);
        // }

        // while (req->sended_bytes < req->max_send_bytes && req->remain_bytes > 0)
        // {
        //     cap >> server_img;
        //     int s = send(req->conn_fd, server_img.data, imgSize, MSG_NOSIGNAL);
        //     req->sended_bytes += s;
        //     req->remain_bytes -= s;
        // }

        // // fprintf(stderr, "remain bytes: %d\n", req->remain_bytes);

        // if (req->remain_bytes == 0)
        // {
        //     cap.release();
        // }
        // else
        // {
        //     req->cap = cap;
        // }

        // chdir("..");
    }
    else if (commands[0] == "ban")
    {
        sprintf(init_msg, "%01023ld", req->username != "admin" ? strlen(permission_denied_msg) + 1 : 0);
        send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL);

        if (req->username != "admin")
            send(req->conn_fd, permission_denied_msg, strlen(permission_denied_msg) + 1, MSG_NOSIGNAL);
        else
            for (int i = 1; i < commands.size(); i++)
                if (commands[i] != "admin")
                    banlist.insert(commands[i]);
    }
    else if (commands[0] == "unban")
    {
        sprintf(init_msg, "%01023ld", req->username != "admin" ? strlen(permission_denied_msg) + 1 : 0);
        send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL);

        if (req->username != "admin")
            send(req->conn_fd, permission_denied_msg, strlen(permission_denied_msg) + 1, MSG_NOSIGNAL);
        else
            for (int i = 1; i < commands.size(); i++)
                banlist.erase(commands[i]);
    }
    else if (commands[0] == "blacklist")
    {
        std::string tmp;

        for (auto i : banlist)
        {
            tmp += i;
            tmp += '\n';
        }
        tmp += '\0';

        sprintf(init_msg, "%01023ld", req->username != "admin" ? strlen(permission_denied_msg) + 1 : tmp.size() + 1);
        send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL);

        if (req->username != "admin")
            send(req->conn_fd, permission_denied_msg, strlen(permission_denied_msg) + 1, MSG_NOSIGNAL);
        else
            send(req->conn_fd, tmp.c_str(), tmp.size() + 1, MSG_NOSIGNAL);
    }
    else
    {
        sprintf(init_msg, "%01023ld", strlen(invalid_command_msg) + 1);
        send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL);
        send(req->conn_fd, invalid_command_msg, strlen(invalid_command_msg) + 1, MSG_NOSIGNAL);
    }

    // fprintf(stderr, "return from process_request\n");
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

    FD_ZERO(&rfd);
    FD_SET(svr.listen_fd, &rfd);

    cliaddr_size = sizeof(cliaddr);
    timeout.tv_sec = 0;
    timeout.tv_usec = 50;

    int time = 0;

    while (1)
    {
        if ((time++) % 200000 == 0)
            fprintf(stderr, "time: %d\n", time / 200000);

        memcpy(&working_set, &rfd, sizeof(rfd));
        if (select(maxfd + 1, &working_set, NULL, NULL, &timeout) < 0)
        {
            ERR_EXIT("select");
        }

        for (int i = 0; i < maxfd; i++)
        {
            if (FD_ISSET(i, &working_set))
            {
                fprintf(stderr, "working_set fd: %d ready\n", i);
                if (i == svr.listen_fd)
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
                                (void)fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                                continue;
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
                else
                {
                    if ((ret = handle_request(&requests[i])) < 0)
                    {
                        fprintf(stderr, "bad request from %s\n", requests[i].username.c_str());
                        FD_CLR(i, &rfd);
                        close(requests[i].conn_fd);
                        free_request(&requests[i]);
                    }
                    else
                    {
                        std::vector<std::string> commands;
                        parse_request(&requests[i], commands);
                        process_request(&requests[i], commands);
                    }
                }
            }
            else if (requests[i].remain_bytes)
            {
                std::vector<std::string> commands;
                parse_request(&requests[i], commands);
                process_request(&requests[i], commands);
            }
        }
    }
}