#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string>
#include <vector>
#include <set>
#include <iostream>

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

char file_size[1024];
struct stat file_stat;

int offset;
int remain_bytes;

std::set<std::string> banlist;

static void init_request(request *req)
{
    req->conn_fd = -1;
    req->buf_len = 0;
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

    maxfd = getdtablesize();
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
        return 0;

    // fprintf(stderr, "buflen: %d buf: %s\n", strlen(buf), buf);
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
    return 1;
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
    if (banlist.find(req->username) != banlist.end()) {
        send(req->conn_fd, "premission denied", 1024, MSG_NOSIGNAL);
    }
    else if (commands[0] == "hello")
    {
        req->username = commands[1];

        char path[1024] = "./";
        for (int i = 0; i < req->username.size(); i++)
        {
            path[i + 2] = req->username[i];
        }
        path[req->username.size() + 2] = '\0';

        mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }
    else if (commands[0] == "ls")
    {
        char path[1024] = "./";
        for (int i = 0; i < req->username.size(); i++)
        {
            path[i + 2] = req->username[i];
        }
        path[req->username.size() + 2] = '\0';

        chdir(path);

        struct dirent *di; // Pointer for directory entry

        // opendir() returns a pointer of DIR type.
        DIR *dir = opendir(".");

        if (dir == NULL) // opendir returns NULL if couldn't open directory
        {
            write(req->conn_fd, "ERROR: Couldn't open directory", 1024);
            ERR_EXIT("Couldn't open directory");
        }

        // for readdir()
        while ((di = readdir(dir)) != NULL)
            send(req->conn_fd, di->d_name, 1024, MSG_NOSIGNAL);

        closedir(dir);

        chdir("..");
    }
    else if (commands[0] == "put")
    {
        char filename[1024];
        sprintf(filename, "./%s", commands[1]);
        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);

        
        if (recv(req->conn_fd, file_size, 1024, 0) < 0) {
            ERR_EXIT("receiving greeting error");
        }

        offset = 0;
        remain_bytes = atoi(file_size);

        while(((recv_bytes = recv(req->conn_fd, req->buf, 1024, 0)) > 0 && (remain_bytes > 0))) {
            remain_bytes -= sent_bytes;
            write(fd, req->buf, 1024);
        }

        close(fd);
    }
    else if (commands[0] == "get")
    {
        char filename[1024], init_msg[1024];
        sprintf(filename, "./%s", commands[1]);
        int fd = open(filename, O_RDONLY);

        if (fstat(fd, &file_stat) < 0) {
            ERR_EXIT("open file error");
        }

        sprintf(init_msg, "sending %d char.", file_stat.st_size);

        if (send(req->conn_fd, init_msg, 1024, MSG_NOSIGNAL) < 0) {
            ERR_EXIT("sending greeting error");
        }

        offset = 0;
        remain_bytes = file_stat.st_size;

        while(((sent_bytes = sendfile(req->conn_fd, fd, &offset, BUFSIZ)) > 0 && (remain_bytes > 0))) {
            remain_bytes -= sent_bytes;
        }
    }
    else if (commands[0] == "play")
    {
        // placeholder
    }
    else if (commands[0] == "ban")
    {
        if (req->username != "admin") {
            send(req->conn_fd, "premission denied", 1024, MSG_NOSIGNAL);
        }
        else {
            for(int i=1;i<commands.size();i++) {
                if (commands[i] != "admin") {
                    banlist.insert(commands[i]);
                }
            }
        }
    }
    else if (commands[0] == "unban")
    {
        if (req->username != "admin") {
            send(req->conn_fd, "premission denied", 1024, MSG_NOSIGNAL);
        }
        else {
            for(int i=1;i<commands.size();i++) {
                banlist.erase(commands[i]);
            }
        }
    }
    else if (commands[0] == "blacklist")
    {
       if (req->username != "admin") {
            send(req->conn_fd, "premission denied", 1024, MSG_NOSIGNAL);
        }
        else {
            for(auto i:banlist) {
                char banned_name[1024];
                sprintf(banned_name, "%s", i);
                send(req->conn_fd, banned_name, 1024, MSG_NOSIGNAL);
            }
        }
    }
    else
    {
        send(req->conn_fd, "invalid command", 1024, MSG_NOSIGNAL);
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



    FD_ZERO(&rfd);
    FD_SET(svr.listen_fd, &rfd);

    cliaddr_size = sizeof(cliaddr);
    timeout.tv_sec = 0;
    timeout.tv_usec = 50;

    while (1)
    {
        memcpy(&working_set, &rfd, sizeof(rfd));
        if (select(maxfd + 1, &working_set, NULL, NULL, &timeout) < 0)
        {
            ERR_EXIT("select");
        }

        for (int i = 0; i < maxfd + 1; i++)
        {
            if (FD_ISSET(i, &working_set))
            {
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
                        FL_SET(conn_fd, O_NONBLOCK);
                    }
                }
                else
                {
                    int ret = handle_request(&requests[i]);
                    std::vector<std::string> commands;
                    parse_request(&requests[i], commands);
                    process_request(&requests[i], commands);
                }
            }
        }
    }
}