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

    if(builtin_cmd_hash[hash].key&&!strcmp(builtin_cmd_hash[hash].key,argv[0])){
        return builtin_cmd_hash[hash].f;
    }

    unsigned now=builtin_cmd_hash[hash].next;
    while(now){
        if(builtin_cmd_hash[hash].key&&!strcmp(builtin_cmd[now].key,argv[0])){
            return builtin_cmd[now].f;
        }
        now=builtin_cmd[now].next;
    }

    return 0;
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

unsigned cmd_str_to_unsigned(const char *str,unsigned long size){
    unsigned r=0,i=0;
    while(i<size){
        if(str[i]<'0'||str[i]>'9'){
            r=-1;
            break;
        }
        r*=10;
        r+=str[i]+'0';
        i++;
    }
    return r;
}

int cmd_execvpe(const char *file, char *const argv[],char *const envp[]){
    if(!access(file,F_OK)){
        return execve(file,argv,envp);
    }
    const char *path=get_var_s("PATH",4);
    unsigned j=0,len=strlen(path),flen=strlen(file);
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

int sh_export(char *const *argv){
    if(!argv[1]){
        unsigned i=0;
        while(env_vec[i]){
            write(STDOUT_FILENO,env_vec[i],strlen(env_vec[i]));
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
                write(STDOUT_FILENO,variable[i],strlen(variable[i]));
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
    return unset_var(argv[1],strlen(argv[1]))?127:0;
}

int sh_read(char *const *argv){
    if(!argv[1]){
        return 127;
    }
    int umask=IN_ECHO|IN_HANDLE_CHAR;
    int i=1;
    const char *var=NULL;
    while(argv[i]){
        if(argv[i][0]=='-'){
            switch(argv[i][1]){
            case 's':
                umask&=~IN_ECHO;
                break;
            case 'r':
                umask&=~IN_HANDLE_CHAR;
                break;
            case 'p':
                if(!argv[i+1]||argv[i+1][0]=='-'){
                    return 127;
                }
                write(STDOUT_FILENO,argv[i+1],strlen(argv[i+1]));
                i++;
                break;
            default:
                return 127;
            }
        }else{
            if(var){
                return 127;
            }
            var=argv[i];
        }
        i++;
    }
    unsigned vlen=strlen(var);
    unsigned len=0;
    int r=input(buffer+vlen+1,BUF_SIZE-vlen-1,&len,umask);
    if(!r){
        return 127;
    }
    memcpy(buffer,var,vlen);
    buffer[vlen]='=';
    set_var(buffer,0);
    return 0;
}
int sh_echo(char *const *argv){
    unsigned i=1;
    while(argv[i]){
        write(STDOUT_FILENO,argv[i],strlen(argv[i]));

        i++;
        if(argv[i]){
            write(STDOUT_FILENO," ",1);
        }else{
            write(STDOUT_FILENO,"\n",1);
        }
    }
    return 0;
}

int sh_jobs(char *const *argv){
    return 0;
}
int sh_fg(char *const *argv){
    return 0;
}
int sh_bg(char *const *argv){
    return 0;
}
int sh_wait(char *const *argv){
    return 0;
}

int sh_test(char *const *argv){
    return 0;
}
int sh_true(char *const *argv){
    return 0;
}
int sh_false(char *const *argv){
    return 0;
}

int sh_command(char *const *argv){
    return 0;
}
int sh_exec(char *const *argv){
    if(!argv[1]){
        return 1;
    }
    cmd_execvpe(argv[1],&argv[1],(char*const*)env_vec);
    write(STDERR_FILENO,argv[1],strlen(argv[1]));
    write(STDERR_FILENO,": command not found\n",20);
    return 127;
}
int sh_eval(char *const *argv){
    return 0;
}
int sh_times(char *const *argv){
    return 0;
}

int sh_trap(char *const *argv){
    return 0;
}
int sh_set(char *const *argv){
    return 0;
}
int sh_shift(char *const *argv){
    return 0;
}
int sh_getopts(char *const *argv){
    return 0;
}

int sh_umask(char *const *argv){
    return 0;
}
int sh_alias(char *const *argv){
    return 0;
}
int sh_unalias(char *const *argv){
    return 0;
}
int sh_type(char *const *argv){
    return 0;
}
