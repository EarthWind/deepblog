#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

typedef struct Link {
    int     data;
    struct  Link    *next;
}slink;

static void usage(const char* prog);
static void init_list(int link_num);
static void display_list(slink *link_ins);
static slink *reverse_list(slink *link_ins);
static void free_list(slink *link_ins);

slink *link_ins;

int main(int argc, char *argv[]){
    int     opt;
    int     link_num;
    slink   *reserved_list;

    if(argc < 2 ){
        usage(argv[0]);
    }
    while((opt = getopt(argc, argv, "n:")) != -1){
        switch(opt){
            case 'n':
                link_num = atoi(optarg);
                break;
            default:
                usage(argv[0]);
        }
    }
    init_list(link_num);
    display_list(link_ins);
    reserved_list = reverse_list(link_ins);
    display_list(reserved_list);

    free_list(reserved_list);

    exit(EXIT_SUCCESS);
}

static void usage(const char* prog){
    printf("Usage: %s options\n", prog);
    printf("options:\n");
    printf("-n\t:element number of link list.\n");

    exit(EXIT_FAILURE);
}

static void init_list(int link_num){
    int     i;
    slink   *cur_node;
    slink   *tmp;

    for(i = 0; i < link_num; i++){
        tmp = (slink *)malloc(sizeof(slink));
        if(i == 0){
            link_ins = tmp;
            cur_node = link_ins;
        }else{
            cur_node -> next = tmp;
            cur_node = tmp;
        }
        tmp -> data = i;
    }
}

static void display_list(slink *link_ins){
    slink *top;

    top = link_ins;
    while(top){
        printf("%d\n", top -> data);
        top = top -> next;
    }
}

static void free_list(slink *link_ins){
    slink *tmp;
    tmp = link_ins;
    while(tmp){
        tmp = link_ins -> next;
        free(link_ins);
        link_ins = tmp;
    }
    printf("free done\n");
}

static slink *reverse_list(slink *link_ins){
    if(link_ins == NULL || link_ins -> next == NULL){
        return link_ins;
    }
    slink *prev = link_ins;
    slink *cur = link_ins -> next;
    slink *tmp;

    while(cur){
        tmp = cur -> next;
        cur -> next = prev;
        prev = cur;
        cur = tmp;
    }
    link_ins -> next = NULL;

    return prev;
}
