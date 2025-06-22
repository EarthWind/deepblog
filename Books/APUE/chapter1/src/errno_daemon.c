#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[]){
        fprintf(stdout, "EACCES: %s\n", strerror(EACCES));
        errno = ENOENT;
        perror(argv[0]);

        exit(EXIT_SUCCESS);
}
