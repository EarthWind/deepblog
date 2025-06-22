#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

int main(int argc, char *argv[]) {
    DIR *dp;
    struct dirent *dirp;

    dp = opendir("/home/yang");
    if(dp == NULL) {
        printf("can not open /home/yang");
        exit(EXIT_FAILURE);
    }
    while((dirp = readdir(dp)) != NULL) {
        printf("%s\n", dirp -> d_name);
    }

    closedir(dp);
    exit(EXIT_SUCCESS);
}
