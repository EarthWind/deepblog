#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]){
        DIR             *dp;
        struct dirent   *dirp;

        if(argc != 2){
                fprintf(stderr, "usage: ls directory_name\n");
                exit(EXIT_FAILURE);
        }

        if((dp  = opendir(argv[1])) == NULL){
                perror("cant't open directory");
                exit(EXIT_FAILURE);
        }
        while((dirp = readdir(dp)) != NULL){
                printf("%s\n", dirp -> d_name);
        }

        closedir(dp);
        exit(EXIT_SUCCESS);
}

