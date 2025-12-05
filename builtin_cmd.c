#include "mnsh.h"
#include <unistd.h>
#include <memory.h>
#include <stdio.h>


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
unsigned cmd_unsigned_to_str(char *str,unsigned long size,unsigned num){
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

int cmd_execvpe(const char *file, char *const argv[],char *const envp[]){
    if(!access(file,F_OK)){
        return execve(file,argv,envp);
    }
    const char *path=get_var_s("PATH",4);
    unsigned j=0,len=cmd_strlen(path),flen=cmd_strlen(file);
    for(unsigned i=0;i<len;i++){
        if(path[i]==':'){
            buffer[j++]='/';
            memcpy(buffer+j,file,flen);
            buffer[j+flen]='\0';
            if(!access(buffer,F_OK)){
                return execve(buffer,argv,envp);
            }
            j=0;
            continue;
        }
        buffer[j++]=path[i];
    }
    buffer[j++]='/';
    memcpy(buffer+j,file,flen);
    buffer[j+flen]='\0';
    if(!access(buffer,F_OK)){
        return execve(buffer,argv,envp);
    }
    return -1;
}

int sh_cd(char *const *argv){
    int r=1;
    if(!argv[1]){
        get_var(buffer,BUF_SIZE,"HOME",NULL,NULL);
        r=chdir(buffer);
    }else{
        r=chdir(argv[1]);
    }
    if(r){
        write(STDOUT_FILENO,"cd: ",4);
        write(STDOUT_FILENO,argv[1],cmd_strlen(argv[1]));
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

int sh_export(char *const *argv){
    if(!argv[1]){
        unsigned i=0;
        while(env_vec[i]){
            write(STDOUT_FILENO,env_vec[i],cmd_strlen(env_vec[i]));
            write(STDOUT_FILENO,"\n",1);
            i++;
        }
        return 0;
    }
    return set_var(argv[1],VAR_EXPORT)?127:0;
}
int sh_readonly(char *const *argv){
    if(!argv[1]){
        for(unsigned i=0;i<VAR_ITEM;i++){
            if(var_umask[i]&VAR_READONLY&&var_umask[i]&VAR_EXIST){
                write(STDOUT_FILENO,variable[i],cmd_strlen(variable[i]));
                write(STDOUT_FILENO,"\n",1);
            }
        }
        return 0;
    }
    return set_var(argv[1],VAR_READONLY)?127:0;
}
int sh_unset(char *const *argv){
    if(!argv[1]){
        return 127;
    }
    return unset_var(argv[1],cmd_strlen(argv[1]))?127:0;
}

int sh_read(char *const *argv){
    if(!argv[1]){
        return 127;
    }
    int echo=1;
    int i=1;
    if(argv[1][0]=='-'){
        i++;
        switch(argv[1][1]){
        case 's':
            echo=0;
            break;
        case 'p':
            if(!argv[2]){
                return 127;
            }
            write(STDOUT_FILENO,argv[2],cmd_strlen(argv[2]));
            i++;
            break;
        default:
            return 127;
        }
    }
    if(!argv[i]){
        return 127;
    }
    return 0;
}
int sh_echo(char *const *argv){
    unsigned i=1;
    while(argv[i]){
        write(STDOUT_FILENO,argv[i],cmd_strlen(argv[i]));

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
    cmd_execvpe(argv[1],&argv[1],(char*const*)env_vec);
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