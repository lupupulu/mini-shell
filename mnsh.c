#include "mnsh.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int now_is_bufffer=1;
int echo=1;
da_str buffer;

size_t pos;

da_history history;
size_t history_pos;

da_variable variable;
da_env env;

char pathbuf[PATHBUFSIZ];

#define output(s,l) if(echo)write(STDOUT_FILENO,s,l)
void insert_str(const char *c,unsigned len);
void insert(char c);
void clean_show(void);

int deal_keys(unsigned char c);


int input(unsigned umask){
    int ret=0;
    char c;
    pos=0;

    history_pos=history.size;

    echo=umask&IN_ECHO;

    while(1){
        ret=read(STDIN_FILENO,&c,1);
        if(c==0x04||ret==0){
            write(STDOUT_FILENO,"\nexit\n",6);
            return -1;
        }

        if(c=='\n'){
            write(STDOUT_FILENO,"\n",1);
            if(!now_is_bufffer){
                free(history.arr[history.size-1]);
                da_pop(sizeof(char*),&history);
            }
            now_is_bufffer=1;
            return 0;
        }else if(deal_keys(c)==-1){
            insert(c);
        }
    }
    return 0;
}

#define STD_RET(v) \
    ((unsigned)(v)>127?(127):(v))

int deal_keys(unsigned char c){
    int ret=0;
    if(c>=0&&!key_config[c].func){
        if(c<32||c==127){
            return 0;
        }
        return -1;
    }
    if(!key_config[c].esc){
        ret=key_config[c].func();
        return STD_RET(ret);
    }

    char buf[16]={},esc[16]={c};
    size_t cnt=0,siz=0;
    key_setting_list_t *now=&key_config[c];

    while(now!=&key_config_list[0]){
        while(cnt<now->len){
            ret=read(STDIN_FILENO,&c,1);
            if(!ret){
                insert_str(esc,siz);
                return 127;
            }
            buf[cnt++]=c;
            if(IS_SHOWN(c)){
                esc[siz++]=c;
            }
        }
        if(now->esc&&!strcmp(buf,now->esc)){
            ret=now->func();
            return STD_RET(ret);
        }
        now=&key_config_list[now->next];
    }
    insert_str(esc,siz);
    return 127;
}


void insert_str(const char *c,unsigned len){
    if(pos==buffer.size){
        if(buffer.size+len>=buffer.real){
            da_resize(sizeof(char),&buffer,buffer.size+len+1);
        }
        memcpy(buffer.arr+buffer.size,c,len);
        buffer.size+=len;
        pos+=len;
        output(c,len);
    }else{
        if(buffer.size+len>=buffer.real){
            da_resize(sizeof(char),&buffer,buffer.size+len+1);
        }
        memmove(buffer.arr+pos+len,buffer.arr+pos,buffer.size-pos);
        memcpy(buffer.arr+pos,c,len);
        buffer.size+=len;
        pos+=len;
        if(!echo){
            return ;
        }
        write(STDOUT_FILENO,buffer.arr+pos-len,buffer.size-pos+len);
        for(int i=0;i<buffer.size-pos;i++){
            write(STDOUT_FILENO,"\b",1);
        }
    }
}



void insert(char c){
    if(pos==buffer.size){
        da_add(sizeof(char),&buffer,&c);
        output(&c,1);
    }else{
        da_add(sizeof(char),&buffer,&c);
        memmove(buffer.arr+pos+1,buffer.arr+pos,buffer.size-pos);
        buffer.arr[pos]=c;
        if(!echo){
            return ;
        }
        write(STDOUT_FILENO,buffer.arr+pos,buffer.size-pos);
        for(int i=0;i<buffer.size-pos-1;i++){
            write(STDOUT_FILENO,"\b",1);
        }
    }
    pos++;
}

void clean_show(void){
    for(size_t i=0;i<pos;i++){
        write(STDOUT_FILENO,"\b",1);
    }
    for(size_t i=0;i<buffer.size;i++){
        write(STDOUT_FILENO," ",1);
    }
    for(size_t i=0;i<buffer.size;i++){
        write(STDOUT_FILENO,"\b",1);
    }
}



int backspace(void){
    if(!pos){
        return 0;
    }
    if(pos==buffer.size){
        da_pop(sizeof(char),&buffer);
        pos--;
        output("\b \b",3);
        return 0;
    }
    memmove(buffer.arr+pos-1,buffer.arr+pos,buffer.size-pos);
    pos--;
    da_pop(sizeof(char),&buffer);
    if(!echo){
        return 0;
    }
    write(STDOUT_FILENO,"\b",1);
    write(STDOUT_FILENO,buffer.arr+pos-1,buffer.size-pos);
    write(STDOUT_FILENO," ",1);
    for(int i=0;i<buffer.size-pos+1;i++){
        write(STDOUT_FILENO,"\b",1);
    }
    return 0;
}

int delete(void){
    if(pos>=buffer.size){
        return 0;
    }
    if(pos==buffer.size-1){
        da_pop(sizeof(char),&buffer);
        output(" \b",2);
        return 0;
    }
    memmove(buffer.arr+pos,buffer.arr+pos+1,buffer.size-pos);
    da_pop(sizeof(char),&buffer);
    
    if(!echo){
        return 0;
    }
    write(STDOUT_FILENO,buffer.arr+pos,buffer.size-pos);
    write(STDOUT_FILENO," ",1);
    for(int i=0;i<buffer.size-pos+1;i++){
        write(STDOUT_FILENO,"\b",1);
    }
    return 0;
}

int left(void){
    if(pos){
        output("\033[D",3);
        pos--;
    }
    return 0;
}

int right(void){
    if(pos<buffer.size){
        output("\033[C",3);
        pos++;
    }
    return 0;
}

int to_start(void){
    while(pos){
        output("\033[D",3);
        pos--;
    }
    return 0;
}

int to_end(void){
    while(pos<buffer.size){
        output("\033[C",3);
        pos++;
    }
    return 0;
}

int last_word(void){
    if(!pos){
        return 0;
    }
    int legal=IS_LEGAL(buffer.arr[pos-1]);
    if(!legal){
        while(pos&&!IS_LEGAL(buffer.arr[pos-1])){
            output("\033[D",3);
            pos--;
        }
    }
    while(pos&&IS_LEGAL(buffer.arr[pos-1])){
        output("\033[D",3);
        pos--;
    }
    return 0;
}

int next_word(void){
    if(pos>=buffer.size){
        return 0;
    }
    int legal=IS_LEGAL(buffer.arr[pos]);
    if(!legal){
        while(pos<buffer.size&&!IS_LEGAL(buffer.arr[pos])){
            pos++;
            output("\033[C",3);
        }
    }
    while(pos<buffer.size&&IS_LEGAL(buffer.arr[pos])){
        pos++;
        output("\033[C",3);
    }
    return 0;
}

int clear_left(void){
    while(pos){
        backspace();
    }
    return 0;
}

int clear_right(void){
    while(pos!=buffer.size){
        delete();
    }
    return 0;
}

int last_history(void){
    if(!history_pos){
        return 0;
    }

    clean_show();

    if(now_is_bufffer){
        char *p=malloc(sizeof(char)*(buffer.size+1));
        memcpy(p,buffer.arr,buffer.size);
        p[buffer.size]='\0';
        da_add(sizeof(char*),&history,&p);
        now_is_bufffer=0;
    }
    history_pos--;
    da_clear(&buffer);
    buffer.size=strlen(history.arr[history_pos]);
    buffer.arr=malloc(buffer.size);
    memcpy(buffer.arr,history.arr[history_pos],buffer.size);
    buffer.real=buffer.size;
    pos=buffer.size;

    output(buffer.arr,buffer.size);
    return 0;
}

int next_history(void){
    if(history_pos>=history.size-1){
        return 0;
    }

    clean_show();

    history_pos++;
    da_clear(&buffer);
    buffer.size=strlen(history.arr[history_pos]);
    buffer.arr=malloc(buffer.size);
    memcpy(buffer.arr,history.arr[history_pos],buffer.size);
    buffer.real=buffer.size;
    pos=buffer.size;
    if(history_pos==history.size-1){
        free(history.arr[history.size-1]);
        da_pop(sizeof(char*),&history);
        now_is_bufffer=1;
    }

    output(buffer.arr,buffer.size);
    return 0;
}


variable_t *find_var(const char *var){
    for(size_t i=0;i<variable.size;i++){
        if(!(variable.arr[i].umask&VAR_EXIST)){
            continue;
        }
        size_t j=0;
        while(j<variable.arr[i].eq_loc&&var[j]&&var[j]==variable.arr[i].var[j]){
            j++;
        }
        if(j==variable.arr[i].eq_loc&&(var[j]=='\0'||var[j]=='=')){
            return &variable.arr[i];
        }
    }
    return NULL;
}
const char *get_var(const char *var,size_t i){
    variable_t *v=find_var(var);
    if(!v){
        return NULL;
    }else if(!(v->umask&VAR_ARRAY)){
        return v->var+v->eq_loc+1;
    }
    return NULL;
}
int set_var(const char *var,char umask){
    variable_t *v=find_var(var);
    size_t len=strlen(var);
    if(umask&VAR_ARRAY){
        while(!var[len-1]&&!var[len]){
            len++;
        }
    }
    if(v){
        if(v->umask&VAR_READONLY){
            return 127;
        }
        void *p=realloc(v->var,len+1);
        if(!p){
            return 127;
        }
        v->var=p;
        memcpy(v->var+v->eq_loc+1,var+v->eq_loc+1,len-v->eq_loc-1);
        v->var[len]='\0';

        if(v->umask&VAR_EXPORT&&!umask&VAR_EXPORT){
            unset_env(v->env);
        }else if(!v->umask&VAR_EXPORT&&umask&VAR_EXPORT){
            v->env=set_env(v->var);
        }

        v->umask=VAR_EXIST|umask;
        return 0;
    }
    for(size_t i=0;i<variable.size;i++){
        if(!(variable.arr[i].umask&VAR_EXIST)){
            v=&variable.arr[i];
        }
    }
    if(!v){
        variable_t tmp={};
        da_add(sizeof(variable_t),&variable,&tmp);
        v=&variable.arr[variable.size-1];
    }
    v->var=malloc((len+1)*sizeof(char));
    memcpy(v->var,var,len);
    v->var[len]='\0';
    for(size_t i=0;i<len;i++){
        if(v->var[i]=='='){
            v->eq_loc=i;
            break;
        }
    }
    if(umask&VAR_EXPORT){
        v->env=set_env(v->var);
    }
    v->umask=VAR_EXIST|umask;
    return 0;
}
int unset_var(const char *var){
    variable_t *v=find_var(var);
    if(!v||(v&&v->umask&VAR_READONLY)){
        return 127;
    }
    if(v->env){
        unset_env(v->env);
    }
    free(v->var);
    *v=variable.arr[variable.size-1];
    return da_pop(sizeof(variable_t),&variable);
}

size_t set_env(char *str){
    env.arr[env.size-1]=str;
    char *tmp=NULL; 
    da_add(sizeof(char*),&env,&tmp);
    return env.size-2;
}

void unset_env(size_t i){
    if(env.size<2){
        return ;
    }
    env.arr[i]=env.arr[env.size-2];
    env.arr[env.size-2]=NULL;
    da_pop(sizeof(char*),&env);
}


MAKE_HASH_FUNCTION

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
    if(file[0]=='.'&&!access(file,F_OK)){
        return execve(file,argv,envp);
    }
    const char *path=get_var("PATH",0);
    unsigned len=strlen(path),flen=strlen(file);
    unsigned buflen=0;
    for(unsigned i=0;i<=len;i++){
        if(path[i]!=':'&&path[i]!='\0'){
            pathbuf[buflen++]=path[i];
            continue;
        }
        pathbuf[buflen++]='/';
        for(size_t j=0;buflen<PATHBUFSIZ&&j<=flen;j++){
            pathbuf[buflen++]=file[j];
        }
        if(!access(pathbuf,F_OK)){
            return execve(pathbuf,argv,envp);
        }
        buflen=0;
        pathbuf[0]='\0';
    }
    return -1;
}

int sh_cd(char *const *argv){
    int r=1;
    if(!argv[1]){
        r=chdir(get_var("HOME",0));
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
    char *r=getcwd(pathbuf,PATHBUFSIZ);
    if(r!=pathbuf){
        write(STDOUT_FILENO,"pwd: failed\n",12);
        return 1;
    }
    write(STDOUT_FILENO,pathbuf,strlen(pathbuf));
    write(STDOUT_FILENO,"\n",1);
    return 0;
}
int sh_history(char *const *argv){
    unsigned int i=0,len=0;
    char buf[16];
    while(i<history.size){
        buf[0]=' ';
        buf[1]=' ';
        len=2;
        len+=cmd_unsigned_to_str(buf+2,12,i+1);
        buf[len++]=' ';
        buf[len++]=' ';
        write(STDOUT_FILENO,buf,len);
        write(STDOUT_FILENO,history.arr[i],strlen(history.arr[i]));
        write(STDOUT_FILENO,"\n",1);

        i++;
    }
    return 0;
}

int sh_export(char *const *argv){
    if(!argv[1]){
        unsigned i=0;
        while(env.arr[i]){
            write(STDOUT_FILENO,env.arr[i],strlen(env.arr[i]));
            write(STDOUT_FILENO,"\n",1);
            i++;
        }
        return 0;
    }
    return set_var(argv[1],VAR_EXPORT)?127:0;
}
int sh_readonly(char *const *argv){
    if(!argv[1]){
        for(unsigned i=0;i<variable.size;i++){
            if(variable.arr[i].umask&VAR_READONLY&&variable.arr[i].umask&VAR_EXIST){
                write(STDOUT_FILENO,variable.arr[i].var,strlen(variable.arr[i].var));
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
    return unset_var(argv[1])?127:0;
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
    size_t vlen=strlen(var);
    int r=input(umask);
    if(!r){
        return 127;
    }
    da_resize(sizeof(char),&buffer,sizeof(char)*(vlen+buffer.size+2));
    memmove(buffer.arr+vlen+1,buffer.arr,buffer.size*sizeof(char));
    memcpy(buffer.arr,var,vlen);
    buffer.arr[vlen]='=';
    buffer.arr[buffer.size-1]='\0';
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
    set_terminal_echo(1);
    cmd_execvpe(argv[1],&argv[1],env.arr);
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


size_t next_char(const char *str,size_t size,size_t pos){
    return pos+1;
}
size_t last_char(const char *str,size_t size,size_t pos){
    return pos-1;
}
