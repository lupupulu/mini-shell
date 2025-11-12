#include "mnsh.h"
#include <unistd.h>
#include <string.h>

static inline int cmd_strcmp(const char *str1,const char *str2);
#ifndef _STRING_H
static inline long unsigned int strlen(const char *str);
#endif

unsigned int hash_function(const char *str){
    if(!str||!*str)return 0;
    unsigned int hash=(unsigned char)*str;
    int len=1;

    while(str[len]&&len<6){
        hash=(hash<<3)^(unsigned char)str[len];
        len++;
    }

    return (hash+len)%HASH_SIZE;
}

command_func is_builtin_cmd(char *const *argv){
    unsigned int hash=hash_function(argv[0]);
    unsigned int len=0;
    while(argv[0][len++]){
        if(len>8){
            return 0;
        }
    }

    if(!cmd_strcmp(builtin_cmd_hash[hash].key,argv[0])){
        return builtin_cmd_hash[hash].f;
    }

    unsigned now=builtin_cmd_hash[hash].next;
    while(now){
        if(!cmd_strcmp(builtin_cmd[now].key,argv[0])){
            return builtin_cmd[now].f;
        }
        now=builtin_cmd[now].next;
    }

    return 0;
}


static inline int cmd_strcmp(const char *str1,const char *str2){
    if(!(str1&&str2)){
        return -1;
    }
    unsigned len=0;
    while(str1[len]&&str2[len]&&str1[len]==str2[len]){
        len++;
    }
    return !(str1[len]==str2[len]&&!str1[len]);
}
#ifndef _STRING_H
static inline long unsigned int strlen(const char *str){
    if(!str){
        return 0;
    }
    long unsigned int len=0;
    while(str[len++]);
    return len-1;
}
#endif

int sh_cd(char *const *argv){
    int r=chdir(argv[1]);
    if(r){
        write(STDOUT_FILENO,"cd: ",4);
        write(STDOUT_FILENO,argv[1],strlen(argv[1]));
        write(STDOUT_FILENO,": no such file or directory\n",28);
        return 1;
    }
    return 0;
}
int sh_pwd(char *const *argv){
    char *r=getcwd(buffer,BUF_SIZE);
    if(r!=buffer){
        write(STDOUT_FILENO,"pwd: failed\n",12);
        return 1;
    }
    write(STDOUT_FILENO,buffer,strlen(buffer));
    write(STDOUT_FILENO,"\n",1);
    return 0;
}

int sh_export(char *const *argv){}
int sh_readonly(char *const *argv){}
int sh_unset(char *const *argv){}

int sh_read(char *const *argv){}
int sh_echo(char *const *argv){}
int sh_printf(char *const *argv){}

int sh_jobs(char *const *argv){}
int sh_fg(char *const *argv){}
int sh_bg(char *const *argv){}
int sh_wait(char *const *argv){}

int sh_test(char *const *argv){}
int sh_true(char *const *argv){}
int sh_false(char *const *argv){}

int sh_command(char *const *argv){}
int sh_exec(char *const *argv){}
int sh_eval(char *const *argv){}
int sh_times(char *const *argv){}

int sh_trap(char *const *argv){}
int sh_set(char *const *argv){}
int sh_shift(char *const *argv){}
int sh_getopts(char *const *argv){}

int sh_umask(char *const *argv){}
int sh_alias(char *const *argv){}
int sh_unalias(char *const *argv){}
int sh_type(char *const *argv){}