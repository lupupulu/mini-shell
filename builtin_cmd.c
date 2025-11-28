#include "mnsh.h"
#include <unistd.h>
#include <memory.h>


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


int cmd_strcmp(const char *str1,const char *str2){
    if(!(str1&&str2)){
        return -1;
    }
    unsigned len=0;
    while(str1[len]&&str2[len]&&str1[len]==str2[len]){
        len++;
    }
    if(str1[len]>str2[len]){
        return 1;
    }else if(str1[len]<str2[len]){
        return -1;
    }
    return 0;
}
long unsigned int cmd_strlen(const char *str){
    if(!str){
        return 0;
    }
    long unsigned int len=0;
    while(str[len++]);
    return len-1;
}
int cmd_unsigned_to_str(char *str,unsigned long size,unsigned num){
    unsigned i=0,tmp=num;
    do{
        tmp/=10;
        i++;
    }while(tmp);
    if(i>=size-1){
        return -1;
    }
    for(unsigned j=0;j<i;j++){
        str[i-j-1]=(num%10)+'0';
        num/=10;
    }
    str[i]='\0';
    return i;
}

int sh_cd(char *const *argv){
    unsigned len=0;
    len=parse_var(buffer,BUF_SIZE,argv[1]);
    int r=chdir(buffer);
    if(r){
        write(STDOUT_FILENO,"cd: ",4);
        write(STDOUT_FILENO,buffer,len);
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
    write(STDOUT_FILENO,buffer,cmd_strlen(buffer));
    write(STDOUT_FILENO,"\n",1);
    return 0;
}

int sh_export(char *const *argv){}
int sh_readonly(char *const *argv){}
int sh_unset(char *const *argv){}

int sh_read(char *const *argv){}
int sh_echo(char *const *argv){
    unsigned i=1,len=0;
    while(argv[i]){
        len=parse_var(buffer,BUF_SIZE,argv[i]);
        write(STDOUT_FILENO,buffer,len);

        i++;
        if(argv[i]){
            write(STDOUT_FILENO," ",1);
        }else{
            write(STDOUT_FILENO,"\n",1);
        }
    }
    return 0;
}
int sh_printf(char *const *argv){}

int sh_jobs(char *const *argv){}
int sh_fg(char *const *argv){}
int sh_bg(char *const *argv){}
int sh_wait(char *const *argv){}

int sh_test(char *const *argv){}
int sh_true(char *const *argv){}
int sh_false(char *const *argv){}

int sh_command(char *const *argv){}
int sh_exec(char *const *argv){
    if(!argv[1]){
        return 1;
    }
    execvp(argv[1],&argv[1]);
    write(STDERR_FILENO,argv[1],cmd_strlen(argv[1]));
    write(STDERR_FILENO,": command not found\n",20);
    return 127;
}
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