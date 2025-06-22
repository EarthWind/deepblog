#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

#define MAXLINE 4096

int main(int argc, char *argv[]){
        char buf[MAXLINE];
        pid_t pid;
        int status;

        printf("%% ");
        while(fgets(buf, MAXLINE, stdin) != NULL){
                if(buf[strlen(buf) -1] == '\n'){
                        buf[strlen(buf) -1] = 0;
                }
                if((pid = fork()) < 0){
                        perror("fork error");
                        exit(EXIT_FAILURE);
                }else if(pid == 0){
                        execlp(buf, buf, (char *)0);
                        fprintf(stderr, "couldn't execute: %s\n", buf);
                        exit(127);
                }

                if((pid = waitpid(pid, &status, 0)) < 0){
                        perror("waitpid error");
                        exit(EXIT_FAILURE);
                }
                printf("%% ");
        }

        exit(EXIT_SUCCESS);
}
