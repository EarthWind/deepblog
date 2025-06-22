#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]){
        int     c;

        while((c = getc(stdin)) != EOF){
                if(putc(c, stdout) == EOF){
                        perror("output error");
                        exit(EXIT_FAILURE);
                }
        }
        if(ferror(stdin)){
                perror("input error");
                exit(EXIT_FAILURE);
        }

        exit(EXIT_SUCCESS);
}
