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

char pathbuf[PATH_BUF_SIZE];
char echobuf[ECHO_BUF_SIZE];
size_t echobufsiz;

#define output(s,l) if(echo)write(STDOUT_FILENO,s,l)
void insert(const char *c,unsigned len);
void clean_show(size_t tpos);
void to_pos(size_t tpos);

int deal_keys(unsigned char c);

#ifdef BASIC_INPUT
int input(unsigned umask){
    int c=0;
    while(1){
        c=getc(stdin);
        if(c=='\n'){
            return 0;
        }else if(c=='\0'||c==-1){
            if(!is_script){
                return -1;
            }
            return 2;
        }
        int tc=c;
        while(tc){
            da_add(sizeof(char),&buffer,&tc);
            tc>>=8;
        }
    }
    return -1;
}
#else
int input(unsigned umask){
    int ret=0;
    char c;
    char buf[8];
    pos=0;

    history_pos=history.size;

    echo=umask&IN_ECHO;

    while(1){
        ret=read(STDIN_FILENO,&c,1);
        if(c==0x04||ret==0){
            if(!is_script){
                echo_to_buf("\nexit\n",6);
                echo_buf_to_stdout();
                return -1;
            }
            return 2;
        }

        if(c=='\n'){
            if(!is_script){
                echo_to_buf("\n",1);
                echo_buf_to_stdout();
            }
            if(!now_is_bufffer){
                free(history.arr[history.size-1]);
                da_pop(sizeof(char*),&history);
            }
            now_is_bufffer=1;
            return 0;
        }else if(deal_keys(c)==-1){
            buf[0]=c;
            ret=get_char_len(c);
            for(int i=1;i<ret;i++){
                read(STDIN_FILENO,&c,1);
                buf[i]=c;
            }
            insert(buf,ret);
        }
    }
    return 0;
}
#endif

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
                insert(esc,siz);
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
    insert(esc,siz);
    return 127;
}


size_t next_char(const char *str,size_t pos){
    switch(codeset){
    case C:
        break;
    case UTF8:
        if(UTF8_CHECK(str[pos],2)){
            return pos+2;
        }else if(UTF8_CHECK(str[pos],3)){
            return pos+3;
        }else if(UTF8_CHECK(str[pos],4)){
            return pos+4;
        }
        break;
    }
    return pos+1;
}

size_t last_char(const char *str,size_t pos){
    switch(codeset){
    case C:
        break;
    case UTF8:
        pos--;
        while(UTF8_CHECK(str[pos],1)){
            pos--;
        }
        return pos;
    }
    return pos-1;
}

size_t get_char_width(const char *c){
    switch(codeset){
    case C:
        return 1;
    case UTF8:
        return utf8_get_char_width(c);
    }
    return 1;
}

size_t get_char_len(char c){
    switch(codeset){
    case C:
        break;
    case UTF8:
        if(UTF8_CHECK(c,2)){
            return 2;
        }else if(UTF8_CHECK(c,3)){
            return 3;
        }else if(UTF8_CHECK(c,4)){
            return 4;
        }
        break;
    }
    return 1;
}


void insert(const char *c,unsigned len){
    if(buffer.size+len>=buffer.real){
        da_resize(sizeof(char),&buffer,buffer.size+len+1);
    }
    memmove(buffer.arr+pos+len,buffer.arr+pos,buffer.size-pos);
    memcpy(buffer.arr+pos,c,len);
    buffer.size+=len;
    size_t i=0;
    output(buffer.arr+pos,buffer.size-pos);
    size_t dpos=pos;
    while(i<len){
        dpos=next_char(buffer.arr,dpos);
        i+=get_char_len(c[i]);
    }
    pos=buffer.size;
    to_pos(dpos);
}

void clean_show(size_t tpos){
    size_t dpos=pos;
    size_t cnt=0,width=0;
    while(dpos<buffer.size){
        width=get_char_width(&buffer.arr[dpos]);
        echo_to_buf("  ",width);
        dpos=next_char(buffer.arr,dpos);
        cnt+=width;
    }
    while(cnt){
        echo_to_buf("\b",1);
        cnt--;
    }
    if(tpos<pos){
        // dpos=last_char(buffer.arr,pos);
        dpos=pos;
        while(dpos>tpos){
            width=get_char_width(&buffer.arr[dpos]);
            echo_to_buf("\b\b",width);
            echo_to_buf("  ",width);
            echo_to_buf("\b\b",width);
            dpos=last_char(buffer.arr,dpos);
        }
        pos=tpos;
    }
    echo_buf_to_stdout();

}

void to_pos(size_t tpos){
    size_t dpos=pos;
    if(tpos>pos){
        while(dpos<tpos){
            dpos=next_char(buffer.arr,dpos);
            echo_to_buf("\033[C\033[C",3*get_char_width(&buffer.arr[dpos]));
        }
    }else if(tpos<pos){
        while(dpos>tpos){
            dpos=last_char(buffer.arr,dpos);
            echo_to_buf("\033[D\033[D",3*get_char_width(&buffer.arr[dpos]));
        }
    }
    pos=dpos;
    echo_buf_to_stdout();
}

void echo_to_buf(const char *str,size_t size){
    if(!echo){
        return ;
    }

    if(size>ECHO_BUF_SIZE){
        echo_buf_to_stdout();
        write(STDOUT_FILENO,str,size);
    }else if(echobufsiz+size>=ECHO_BUF_SIZE){
        echo_buf_to_stdout();
        memcpy(echobuf,str,size);
        echobufsiz+=size;
    }else{
        memcpy(echobuf+echobufsiz,str,size);
        echobufsiz+=size;
    }
}

void echo_buf_to_stdout(void){
    if(!echo){
        return ;
    }

    write(STDOUT_FILENO,echobuf,echobufsiz);
    echobufsiz=0;
}


int backspace(void){
    if(!pos){
        return 0;
    }
    size_t dpos=last_char(buffer.arr,pos),opos=pos;
    to_pos(dpos);
    clean_show(pos);
    memmove(buffer.arr+dpos,buffer.arr+opos,buffer.size-opos);
    for(int i=0;i<opos-dpos;i++){
        da_pop(sizeof(char),&buffer);
    }
    output(buffer.arr+pos,buffer.size-pos);
    pos=buffer.size;
    to_pos(dpos);
    return 0;
}

int delete(void){
    if(pos>=buffer.size){
        return 0;
    }

    size_t dpos=next_char(buffer.arr,pos);
    if(pos==buffer.size-1){
        clean_show(pos);
        for(int i=0;i<dpos-pos;i++){
            da_pop(sizeof(char),&buffer);
        }
        return 0;
    }

    clean_show(pos);
    memmove(buffer.arr+pos,buffer.arr+dpos,buffer.size-dpos);
    for(int i=0;i<dpos-pos;i++){
        da_pop(sizeof(char),&buffer);
    }

    output(buffer.arr+pos,buffer.size-pos);

    dpos=pos;
    pos=buffer.size;
    to_pos(dpos);

    return 0;
}

int left(void){
    if(!pos){
        return 0;
    }
    size_t dpos=last_char(buffer.arr,pos);
    output("\033[D\033[D",3*get_char_width(&buffer.arr[dpos]));
    pos=dpos;
    return 0;
}

int right(void){
    if(pos>=buffer.size){
        return 0;
    }
    size_t dpos=next_char(buffer.arr,pos);
    output("\033[C\033[C",3*get_char_width(&buffer.arr[pos]));
    pos=dpos;
    return 0;
}

int to_start(void){
    to_pos(0);
    return 0;
}

int to_end(void){
    to_pos(buffer.size);
    return 0;
}

int last_word(void){
    if(!pos){
        return 0;
    }
    size_t dpos=last_char(buffer.arr,pos);
    int legal=IS_LEGAL(buffer.arr[dpos]);
    if(!legal){
        while(dpos&&!IS_LEGAL(buffer.arr[dpos])){
            dpos=last_char(buffer.arr,dpos);
        }
    }
    while(dpos&&IS_LEGAL(buffer.arr[dpos])){
        dpos=last_char(buffer.arr,dpos);
    }
    if(!IS_LEGAL(buffer.arr[dpos])){
        dpos=next_char(buffer.arr,dpos);
    }
    to_pos(dpos);
    return 0;
}

int next_word(void){
    if(pos>=buffer.size){
        return 0;
    }
    int legal=IS_LEGAL(buffer.arr[pos]);
    size_t dpos=pos;
    if(!legal){
        while(dpos<buffer.size&&!IS_LEGAL(buffer.arr[dpos])){
            dpos=next_char(buffer.arr,dpos);
        }
    }
    while(dpos<buffer.size&&IS_LEGAL(buffer.arr[dpos])){
        dpos=next_char(buffer.arr,dpos);
    }
    to_pos(dpos);
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

int clear_last_word(void){
    if(!pos){
        return 0;
    }
    size_t dpos=last_char(buffer.arr,pos);
    int legal=IS_LEGAL(buffer.arr[dpos]);
    if(!legal){
        while(dpos&&!IS_LEGAL(buffer.arr[dpos])){
            dpos=last_char(buffer.arr,dpos);
        }
    }
    while(dpos&&IS_LEGAL(buffer.arr[dpos])){
        dpos=last_char(buffer.arr,dpos);
    }
    if(!IS_LEGAL(buffer.arr[dpos])){
        dpos=next_char(buffer.arr,dpos);
    }
    size_t opos=pos;
    to_pos(dpos);
    clean_show(pos);
    memmove(buffer.arr+dpos,buffer.arr+opos,buffer.size-opos);
    for(size_t i=0;i<opos-dpos;i++){
        da_pop(sizeof(char),&buffer);
    }
    output(buffer.arr,buffer.size-pos);
    pos=buffer.size;
    to_pos(dpos);
    return 0;
}

int last_history(void){
    if(!history_pos){
        return 0;
    }

    clean_show(0);

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
    if(!history.size||history_pos>=history.size-1){
        return 0;
    }

    clean_show(0);

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
const char *get_var(const char *var){
    if(var[1]=='\0'){
        if(var[0]>='0'&&var[0]<='9'){
            if(var[0]-'0'>g_argc){
                return NULL;
            }else{
                return g_argv[var[0]-'0'];
            }
        }else if(var[0]=='#'){
            return g_argc_s;
        }else if(var[0]=='?'){
            cmd_unsigned_to_str(g_ret_s,MAX_LL_SIZE-1,g_ret);
            return g_ret_s;
        }
    }
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

const char *file_is_exist(const char *file,int type,int is_cmd){
    if(!file){
        return NULL;
    }
    size_t buflen=0;
    size_t flen=strlen(file);
    int is_file=1;
    size_t i=0;
    if(is_cmd){
        is_file=0;
        while(file[i]){
            if(file[i]=='/'){
                is_file=1;
                break;
            }
            i++;
        }
    }
    if(is_file&&!access(file,type)){
        return file;
    }
    const char *path=get_var("PATH");
    size_t len=strlen(path);
    for(unsigned i=0;i<=len;i++){
        if(path[i]!=':'&&path[i]!='\0'){
            pathbuf[buflen++]=path[i];
            continue;
        }
        pathbuf[buflen++]='/';
        for(size_t j=0;buflen<PATH_BUF_SIZE&&j<=flen;j++){
            pathbuf[buflen++]=file[j];
        }
        if(!access(pathbuf,type)){
            return pathbuf;
        }
        buflen=0;
        pathbuf[0]='\0';
    }
    return NULL;
}

int is_variable(const char *cmd){
    if(cmd[0]>='0'&&cmd[0]<='9'){
        return 0;
    }
    size_t i=0;
    while(cmd[i]!='\0'){
        if(cmd[i]=='='){
            return 1;
        }else if(!IS_LEGAL(cmd[i])){
            return 0;
        }
        i++;
    }
    return 0;
}

int cmd_execvpe(const char *file, char *const argv[],char *const envp[]){
    const char *p=file_is_exist(file,F_OK|X_OK,1);
    if(p){
        return execve(p,argv,envp);
    }
    return -1;
}

int sh_cd(char *const *argv){
    int r=1;
    const char *p;
    if(!argv[1]){
        p=get_var("HOME");
    }else{
        p=argv[1];
    }
    r=chdir(p);
    if(r){
        write(STDERR_FILENO,"cd: ",4);
        write(STDERR_FILENO,argv[1],strlen(argv[1]));
        write(STDERR_FILENO,": no such file or directory\n",28);
        return 1;
    }
    pathbuf[0]='\0';
    strcpy(pathbuf,"PWD=");
    strcat(pathbuf,p);
    set_var(pathbuf,VAR_EXPORT);
    return 0;
}
int sh_pwd(char *const *argv){
    char *r=getcwd(pathbuf,PATH_BUF_SIZE);
    if(r!=pathbuf){
        write(STDERR_FILENO,"pwd: failed\n",12);
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
            write(STDOUT_FILENO,"export ",7);
            write(STDOUT_FILENO,env.arr[i],strlen(env.arr[i]));
            write(STDOUT_FILENO,"\n",1);
            i++;
        }
        return 0;
    }

    int ret=is_variable(argv[1]);
    if(!ret){
        return 127;
    }
    return set_var(argv[1],VAR_EXPORT)?127:0;
}
int sh_readonly(char *const *argv){
    if(!argv[1]){
        for(unsigned i=0;i<variable.size;i++){
            if(variable.arr[i].umask&VAR_READONLY&&variable.arr[i].umask&VAR_EXIST){
                write(STDOUT_FILENO,"readonly ",9);
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

    da_fake_clear(&buffer);
    int r=input(umask);
    if(r){
        return 127;
    }

    char *tmp=malloc(sizeof(char)*(strlen(var)+1+buffer.size+1));
    tmp[0]='\0';
    strcpy(tmp,var);
    strcat(tmp,"=");
    da_add(sizeof(char),&buffer,"");
    strcat(tmp,buffer.arr);
    set_var(tmp,0);
    free(tmp);
    da_fake_clear(&buffer);
    return 0;
}
int sh_echo(char *const *argv){
    unsigned i=1;
    while(argv[i]){
        write(STDOUT_FILENO,argv[i],strlen(argv[i]));

        i++;
        if(argv[i]){
            write(STDOUT_FILENO," ",1);
        }
    }
    write(STDOUT_FILENO,"\n",1);
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


