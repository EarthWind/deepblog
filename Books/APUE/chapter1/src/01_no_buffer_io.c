#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BUFFSIZE 4096

int main(int argc, char *argv[]) {
    int n;
    char buf[BUFFSIZE];

    while((n = read(STDIN_FILENO, buf, BUFFSIZE)) > 1) {
        if(write(STDOUT_FILENO, buf, n) != n) {
            printf("write %d bytes to stdout error", n);
            exit(EXIT_FAILURE);
        }
    }

    if( n < 0) {
        printf("read error");
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
