#include "mnsh.h"
#include <stdio.h>

hash_pair_t builtin_cmd[HASH_SIZE];
unsigned int extra_builtin_real=1;
hash_pair_t builtin_cmd_hash[HASH_SIZE];

char buffer[BUF_SIZE];

char start='$';


int register_builtin(const char *name,command_func f);
int init(void);


int init(void){
    int r=0;

    r|=register_builtin("cd",NULL);
    r|=register_builtin("pwd",NULL);
    r|=register_builtin("history",NULL);

    r|=register_builtin("export",NULL);
    r|=register_builtin("readonly",NULL);
    r|=register_builtin("unset",NULL);

    r|=register_builtin("read",NULL);
    r|=register_builtin("echo",NULL);

    r|=register_builtin("jobs",NULL);
    r|=register_builtin("fg",NULL);
    r|=register_builtin("bg",NULL);
    r|=register_builtin("wait",NULL);

    r|=register_builtin("test",NULL);
    r|=register_builtin("true",NULL);
    r|=register_builtin("false",NULL);

    r|=register_builtin("command",NULL);
    r|=register_builtin("exec",NULL);
    r|=register_builtin("eval",NULL);
    r|=register_builtin("times",NULL);

    r|=register_builtin("trap",NULL);
    r|=register_builtin("set",NULL);
    r|=register_builtin("shift",NULL);
    r|=register_builtin("getopts",NULL);

    r|=register_builtin("umask",NULL);
    r|=register_builtin("alias",NULL);
    r|=register_builtin("unalias",NULL);
    r|=register_builtin("type",NULL);

    return r;
}

int main(int argc,const char *argv[]){
    init();

    FILE *fp=fopen("config.c","w");

    fprintf(fp,"#include \"mnsh.h\"\n\n");

    fprintf(fp,"hash_pair_t builtin_cmd[%u]={\n",extra_builtin_real);
    fprintf(fp,"\t{},\n");
    for(int i=1;i<extra_builtin_real;i++){
        fprintf(fp,"\t{.key=\"%s\",.f=sh_%s,.next=%u}",builtin_cmd[i].key,builtin_cmd[i].key,builtin_cmd[i].next);
        if(i<extra_builtin_real-1){
            fprintf(fp,",\n");
        }else{
            fprintf(fp,"\n");
        }
    }
    fprintf(fp,"};\n\n");

    fprintf(fp,"hash_pair_t builtin_cmd_hash[HASH_SIZE]={\n");
    for(int i=0;i<HASH_SIZE;i++){
        if(builtin_cmd_hash[i].key==NULL){
            fprintf(fp,"\t{}");
        }else{
            fprintf(fp,"\t{.key=\"%s\",.f=sh_%s,.next=%u}",builtin_cmd_hash[i].key,builtin_cmd_hash[i].key,builtin_cmd_hash[i].next);
        }
        if(i<HASH_SIZE-1){
            fprintf(fp,",\n");
        }else{
            fprintf(fp,"\n");
        }
    }
    fprintf(fp,"};\n\n");

    fclose(fp);
    return 0;
}

int register_builtin(const char *name,command_func f){
    unsigned hash=hash_function(name);
    if(builtin_cmd_hash[hash].key){
        printf("repeat: %d %s %s\n",hash,builtin_cmd_hash[hash].key,name);
        int now=hash;
        while(builtin_cmd_hash[now].next){
            now=builtin_cmd_hash[now].next;
        }
        builtin_cmd_hash[now].next=extra_builtin_real;
        builtin_cmd[extra_builtin_real].key=name;
        builtin_cmd[extra_builtin_real].f=f;
        builtin_cmd[extra_builtin_real].next=0;
        extra_builtin_real++;
        return 1;
    }
    builtin_cmd_hash[hash].key=name;
    builtin_cmd_hash[hash].f=f;
    builtin_cmd_hash[hash].next=0;
    return 0;
}
