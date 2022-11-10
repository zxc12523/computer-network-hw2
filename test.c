#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

struct stat file_stat;

int main()
{
    char buf[1024];
    sprintf(buf, "%200ld%200ld%200ld%200ld%200ld%23d", 1977, 1, 2, 3, 4, 0);
    fprintf(stderr, "%s len: %d atoi: %d\n", buf, strlen(buf), atoi(buf));
    fprintf(stderr, "%s len: %d atoi: %d\n", buf, strlen(buf), atoi(buf + 200));
    fprintf(stderr, "%s len: %d atoi: %d\n", buf, strlen(buf), atoi(buf + 400));
    fprintf(stderr, "%s len: %d atoi: %d\n", buf, strlen(buf), atoi(buf + 600));
    fprintf(stderr, "%s len: %d atoi: %d\n", buf, strlen(buf), atoi(buf + 800));

    char buffer[8192];

    printf("%ld\n", sizeof(buffer));

    return 0;
}