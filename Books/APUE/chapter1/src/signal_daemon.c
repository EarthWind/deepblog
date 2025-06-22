#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#define MAXLINE 4096

static void sig_int(int);

int main(int argc, char *argv[]){
        char buf[MAXLINE];
        pid_t pid;
        int status;

        if(signal(SIGINT, sig_int) == SIG_ERR){
                perror("signal error");
                exit(EXIT_FAILURE);
        }

        printf("%% ");
        while(fgets(buf, MAXLINE, stdin) != NULL){
                if(buf[strlen(buf) - 1] == '\n'){
                        buf[strlen(buf) - 1] = 0;
                }

                if((pid = fork()) < 0){
                        perror("fork error");
                        exit(EXIT_FAILURE);
                }else if(pid == 0){
                        execlp(buf, buf, (char *)0);
                        fprintf(stderr, "cant't execute command %s\n", buf);
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

static void sig_int(int signo){
        printf("interrupt\n%% ");
}