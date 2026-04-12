#include "mnsh.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <locale.h>
#include <wchar.h>

int g_argc;
char* const* g_argv;
void *g_argv_var;
int g_ret;
int g_pid;

int now_pid;
char *now_name;

int is_script;
int is_child;

int now_is_bufffer=1;
int echo=1;
da_str buffer;

size_t pos;

da_history history;
size_t history_pos;

da_variable variable;
da_env env;
da_variable tmp_env;
da_alias alias;
da_job job;


jobmsg_t jobmsg[JOB_MSG_SIZE];
size_t jobmsgsiz;

char pathbuf[PATH_BUF_SIZE];
char pwd_pathbuf[PATH_BUF_SIZE];
char echobuf[ECHO_BUF_SIZE];
size_t echobufsiz;

#define output(s,l) if(echo)write(STDOUT_FILENO,s,l)
void insert(const char *c,unsigned len);
void clean_show(size_t tpos);
void to_pos(size_t tpos);
inline static void deal_job_status(job_t *job,int status);

int deal_keys(unsigned char c);

int input_basic(void){
    int c=0;
    while(1){
        c=getc(stdin);
        if(c=='\n'){
            return 0;
        }else if(c==EOF){
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
int input(unsigned umask){
    if(is_script){
        return input_basic();
    }
    int ret=0;
    char c;
    char buf[8];
    pos=0;

    history_pos=history.size;

    echo=umask&IN_ECHO;

    while(1){
        ret=read(STDIN_FILENO,&c,1);
        if(ret==-1){
            continue;
        }
        if(c==0x04||ret==0){
            if(!is_script){
                return -1;
            }
            return 2;
        }

        if(c=='\n'){
            if(!is_script){
                echo_to_buf("\n",1);
                echo_buf_to_fd(STDOUT_FILENO);
            }
            if(!now_is_bufffer){
                free(history.arr[history.size-1]);
                da_pop(sizeof(char*),&history);
            }
            now_is_bufffer=1;
            return 0;
        }else if(deal_keys(c)==-1){
            memset(buf,0,sizeof(buf));
            buf[0]=c;
            size_t i=1;
            while((ret=get_char_len(buf))<0){
                read(STDIN_FILENO,&c,1);
                buf[i++]=c;
            }
            insert(buf,ret);
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


size_t next_char(const char *str,size_t pos,size_t size){
    wchar_t wc;
    int r=mbtowc(&wc,str+pos,MB_CUR_MAX);
    if(r<=0){
        return pos;
    }
    return pos+r;
}

size_t last_char(const char *str,size_t pos){
    if(!pos){
        return 0;
    }
    wchar_t wc;
    size_t i=1;
    while(pos-i&&mbtowc(&wc,str+pos-i,MB_CUR_MAX)<0){
        i++;
    }
    return pos-i;
}

size_t get_char_width(const char *c){
    wchar_t wc;
    int r=mbtowc(&wc,c,MB_CUR_MAX);
    if(r<0){
        return 0;
    }
    return wcwidth(wc);
}

size_t get_char_len(const char *c){
    wchar_t wc;
    int r=mbtowc(&wc,c,MB_CUR_MAX);
    return r;
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
        dpos=next_char(buffer.arr,dpos,buffer.size);
        i+=get_char_len(&c[i]);
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
        dpos=next_char(buffer.arr,dpos,buffer.size);
        cnt+=width;
    }
    while(cnt){
        echo_to_buf("\b",1);
        cnt--;
    }
    if(tpos<pos){
        dpos=pos;
        if(dpos==buffer.size){
            echo_to_buf("\b \b",3);
            dpos=last_char(buffer.arr,dpos);
        }
        while(dpos>tpos){
            width=get_char_width(&buffer.arr[dpos]);
            echo_to_buf("\b\b",width);
            echo_to_buf("  ",width);
            echo_to_buf("\b\b",width);
            dpos=last_char(buffer.arr,dpos);
        }
        pos=tpos;
    }
    echo_buf_to_fd(STDOUT_FILENO);

}

void to_pos(size_t tpos){
    size_t dpos=pos;
    if(tpos>pos){
        while(dpos<tpos){
            echo_to_buf("\033[C\033[C",3*get_char_width(&buffer.arr[dpos]));
            dpos=next_char(buffer.arr,dpos,buffer.size);
        }
    }else if(tpos<pos){
        while(dpos>tpos){
            dpos=last_char(buffer.arr,dpos);
            echo_to_buf("\033[D\033[D",3*get_char_width(&buffer.arr[dpos]));
        }
    }
    pos=dpos;
    echo_buf_to_fd(STDOUT_FILENO);
}

void echo_unsigned_to_buf(size_t num){
    if(!echo){
        return ;
    }
    static char tmp[MAX_LL_SIZE];
    unsigned l=cmd_unsigned_to_str(tmp,MAX_LL_SIZE,num);
    echo_to_buf(tmp,l);
}

void echo_to_buf(const char *str,size_t size){
    if(!echo){
        return ;
    }

    if(size>ECHO_BUF_SIZE){
        echo_buf_to_fd(STDOUT_FILENO);
        write(STDOUT_FILENO,str,size);
    }else if(echobufsiz+size>=ECHO_BUF_SIZE){
        echo_buf_to_fd(STDOUT_FILENO);
        memcpy(echobuf,str,size);
        echobufsiz+=size;
    }else{
        memcpy(echobuf+echobufsiz,str,size);
        echobufsiz+=size;
    }
}

void echo_buf_to_fd(int fd){
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

    size_t dpos=next_char(buffer.arr,pos,buffer.size);
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
    size_t dpos=next_char(buffer.arr,pos,buffer.size);
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
        dpos=next_char(buffer.arr,dpos,buffer.size);
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
            dpos=next_char(buffer.arr,dpos,buffer.size);
        }
    }
    while(dpos<buffer.size&&IS_LEGAL(buffer.arr[dpos])){
        dpos=next_char(buffer.arr,dpos,buffer.size);
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
        dpos=next_char(buffer.arr,dpos,buffer.size);
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


int cm_init(command_t *cm){
    memset(cm,0,sizeof(command_t));
    return 0;
}
int cm_add_item(command_t *cm,char *item){
    void *p=realloc(cm->argv,(cm->argvn+1)*sizeof(char*));
    if(!p){
        return 127;
    }
    cm->argv=p;
    cm->argv[cm->argvn]=item;
    cm->argvn++;
    return 0;
}
int cm_add_cmd(command_t *cm,void *v,size_t size){
    if(!cm->cmdsn){
        cm->cmds=NULL;
    }
    cm->cmds=realloc(cm->cmds,sizeof(int*)*(cm->cmdsn+1));
    cm->cmds[cm->cmdsn]=malloc(size);
    memcpy(cm->cmds[cm->cmdsn],v,size);
    cm->cmdsn++;
    return 0;
}
int cm_clear(command_t *cm){
    free(cm->argv);
    for(size_t i=0;i<cm->cmdsn;i++){
        if(*cm->cmds[i]==CMD_VAR){
            command_var_t *v=(void*)cm->cmds[i];
            free(v->var);
        }else if(*cm->cmds[i]==CMD_REDIR){
            command_redir_t *r=(void*)cm->cmds[i];
            free(r->_2);
        }
        free(cm->cmds[i]);
    }
    free(cm->cmds);
    memset(cm,0,sizeof(command_t));
    return 0;
}


command_func get_builtin_cmd(const char *cmd){
    size_t i=strarr_find(sizeof(builtincmd_t),&builtincmd,cmd,strcmp);
    return i==STRARR_CANNOT_FIND?NULL:builtincmd.arr[i].f;
}


int varcmp(const char *a,const char *b){
    size_t len=0;
    while(a[len]&&b[len]&&a[len]==b[len]&&a[len]!='='){
        len++;
    }
    char la=a[len]=='='?'\0':a[len];
    char lb=b[len]=='='?'\0':b[len];
    if(la!=lb){
        return la<lb?-1:1;
    }
    return 0;
}
var_t get_var(const char *var){
    static char ret[MAX_LL_SIZE];
    if(var[0]>='0'&&var[0]<='9'){
        num_t r=cmd_str_to_num(var);
        if(r.is_negative||r.unexcepted_char){
            goto L;
        }
        if(r.num>g_argc){
            return (var_t){.value=NULL,.umask=0};
        }else{
            return (var_t){.value=g_argv[r.num],.umask=VAR_EXPORT|VAR_EXIST};
        }
    }
    L:
    if(var[1]=='\0'){
        if(var[0]=='#'){
            cmd_unsigned_to_str(ret,MAX_LL_SIZE,g_argc);
            return (var_t){.value=ret,.umask=VAR_EXPORT|VAR_EXIST};
        }else if(var[0]=='?'){
            cmd_unsigned_to_str(ret,MAX_LL_SIZE,g_ret);
            return (var_t){.value=ret,.umask=VAR_EXPORT|VAR_EXIST};
        }else if(var[0]=='$'){
            cmd_unsigned_to_str(ret,MAX_LL_SIZE,g_argc);
            return (var_t){.value=ret,.umask=VAR_EXPORT|VAR_EXIST};
        }else if(var[0]=='@'){
            return (var_t){.value=g_argv_var,.umask=VAR_EXIST|VAR_ARRAY|VAR_EXPAND|VAR_EXPAND_IN_QUOTE};
        }else if(var[0]=='*'){
            return (var_t){.value=g_argv_var,.umask=VAR_EXIST|VAR_ARRAY|VAR_EXPAND};
        }
    }
    size_t i=strarr_find(sizeof(variable_t),&variable,var,varcmp);
    if(i==STRARR_CANNOT_FIND){
        return (var_t){.value=NULL,.umask=0};
    }else{
        return (var_t){.value=variable.arr[i].var+variable.arr[i].eq_loc+1,.umask=variable.arr[i].umask};
    }
    return (var_t){.value=NULL,.umask=0};
}
const char *gets_var(const char *var){
    var_t r=get_var(var);
    if(r.umask&(VAR_INT|VAR_FUNC)){
        return NULL;
    }
    if(r.umask&VAR_ARRAY){
        return *(const char**)(r.value+sizeof(size_t));
    }
    return r.value;
}
int set_var(const char *var,char umask){
    size_t len=0;
    size_t eq_loc=0;
    while(var[eq_loc]!='='){
        eq_loc++;
    }
    if(umask&VAR_ARRAY){
        if(umask&VAR_INT){
            var_arr_t(var_int_t) *arr=(void*)&var[eq_loc+1];
            len=eq_loc+VAR_ARR_SIZE(var_int_t,arr);
        }else if(umask&VAR_FUNC){
            var_arr_t(var_func_t) *arr=(void*)&var[eq_loc+1];
            len=eq_loc+VAR_ARR_SIZE(var_func_t,arr);
        }else{
            var_arr_t(char*) *arr=(void*)&var[eq_loc+1];
            len=eq_loc+VAR_ARR_SIZE(char*,arr);
        }
    }else if(umask&VAR_INT){
        len=eq_loc+sizeof(var_int_t);
    }else if(umask&VAR_FUNC){
        len=eq_loc+sizeof(var_func_t);
    }else{
        len=strlen(var)+1;
    }

    size_t i=strarr_find_loc(sizeof(variable_t),&variable,var,varcmp);
    if(i<variable.size&&!varcmp(var,variable.arr[i].var)){
        variable_t *v=&variable.arr[i];
        if(v->umask&VAR_READONLY){
            return 127;
        }
        void *p=realloc(v->var,len);
        if(!p){
            return 127;
        }
        v->var=p;
        memcpy(v->var+v->eq_loc+1,var+v->eq_loc+1,len-v->eq_loc-1);

        if(v->umask&VAR_EXPORT&&!(umask&VAR_EXPORT)){
            unset_env(v->env);
        }else if(!(v->umask&VAR_EXPORT)&&umask&VAR_EXPORT){
            v->env=set_env(v->var);
        }

        v->umask=VAR_EXIST|umask;
        return 0;
    }

    variable_t v={.eq_loc=eq_loc};
    if(!v.eq_loc){
        return 0;
    }

    v.var=malloc(len);
    memcpy(v.var,var,len);
    if(umask&VAR_EXPORT){
        v.env=set_env(v.var);
    }
    v.umask=umask|VAR_EXIST;

    da_add_loc(sizeof(variable_t),&variable,&v,i);
    return 0;
}
int clear_var(void *v,unsigned umask){
    if(!(umask&(VAR_FUNC|VAR_INT|VAR_ARRAY))||umask&VAR_INT){
        return 0;
    }
    size_t size=0;
    if(umask&VAR_ARRAY){
        size=*(size_t*)v;
        v=v+sizeof(size_t);
    }
    for(size_t i=0;i<size;i++){
        if(umask&VAR_FUNC){
            for(size_t j=0;j<((var_func_t*)v)->size;j++){
                cm_clear(&(*(var_func_t**)v)->value[i]);
            }
            v+=sizeof(var_func_t);
        }else{
            free(*(char**)v);
            v+=sizeof(char*);
        }
    }
    return 0;
}
int unset_var(const char *var){
    size_t i=strarr_find(sizeof(variable_t),&variable,var,varcmp);
    if(i==STRARR_CANNOT_FIND||variable.arr[i].umask&VAR_READONLY){
        return 127;
    }
    if(variable.arr[i].umask&VAR_EXPORT){
        unset_env(variable.arr[i].env);
    }
    clear_var(variable.arr[i].var,variable.arr[i].umask);
    free(variable.arr[i].var);
    return da_del(sizeof(variable_t),&variable,i);
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

int set_tmp_env(char *str){
    size_t i=strarr_find(sizeof(variable_t),&variable,str,varcmp);
    if(i!=STRARR_CANNOT_FIND){
        variable_t *var=&variable.arr[i];
        if(var->umask&VAR_READONLY){
            return 127;
        }
        da_add(sizeof(variable_t),&tmp_env,var);
        size_t len=strlen(var->var);
        void *p=malloc(sizeof(char)*(len+1));
        memcpy(p,var->var,sizeof(char)*(len+1));
        var->var=p;
        set_var(str,VAR_EXPORT);
        return 0;
    }
    variable_t tmp={.var=str,.umask=0,.env=1,.eq_loc=0};
    da_add(sizeof(variable_t),&tmp_env,&tmp);
    set_var(str,VAR_EXPORT);
    return 0;
}
int recovery_tmp_env(void){
    for(size_t i=0;i<tmp_env.size;i++){
        if(tmp_env.arr[i].umask){
            size_t j=strarr_find(sizeof(variable_t),&variable,tmp_env.arr[i].var,varcmp);
            variable_t *var=&variable.arr[j];
            free(var->var);
            memcpy(var,&tmp_env.arr[i],sizeof(variable_t));
        }else{
            unset_var(tmp_env.arr[i].var);
        }
    }
    da_clear(&tmp_env);
    return 0;
}

const char *get_alias(const char *als){
    size_t i=strarr_find(sizeof(alias_t),&alias,als,varcmp);
    return i==STRARR_CANNOT_FIND?NULL:alias.arr[i].var+alias.arr[i].eq_loc+1;
}
int set_alias(const char *als){
    if(is_script){
        return 0;
    }

    size_t i=strarr_find_loc(sizeof(alias_t),&alias,als,varcmp);
    if(i<alias.size&&!varcmp(als,alias.arr[i].var)){
        alias_t *a=&alias.arr[i];
        size_t len=strlen(als);
        a->var=realloc(a->var,sizeof(char)*(len+1));
        memcpy(a->var,als,(len+1)*sizeof(char));
        return 0;
    }
    size_t len=strlen(als);
    alias_t tmp={
        .var=malloc(sizeof(char)*(len+1)),
        .eq_loc=0
    };
    memcpy(tmp.var,als,sizeof(char)*(len+1));
    while(tmp.var[tmp.eq_loc]!='='){
        tmp.eq_loc++;
    }

    da_add_loc(sizeof(alias_t),&alias,&tmp,i);

    return 0;
}
int unset_alias(const char *als){
    size_t i=strarr_find(sizeof(alias_t),&alias,als,varcmp);
    if(i==STRARR_CANNOT_FIND){
        return 127;
    }
    free(alias.arr[i].var);
    da_del(sizeof(alias_t),&alias,i);
    return 0;
}

void restore_cmd_redir(da_str *str,command_redir_t *r){
    if(r->_1>=0){
        int fd=(size_t)r->_1;
        int a=fd;
        size_t len=0;
        do{a/=10,len++;}while(a);
        for(size_t i=0;i<len;i++){
            char c=fd%10+'0';
            da_add_loc(sizeof(char),str,&c,0);
        }
    }

    switch(r->op){
    case REDIR_IN:
        da_add(sizeof(char),str,"<");
        break;
    case REDIR_OUT:
        da_add(sizeof(char),str,">");
        break;
    case REDIR_OUT_ADD:
        da_add(sizeof(char),str,">");
        da_add(sizeof(char),str,">");
        break;
    case REDIR_DUP:
        da_add(sizeof(char),str,">");
        da_add(sizeof(char),str,"&");
        break;
    case REDIR_HERE_DOCUMENT:
        da_add(sizeof(char),str,"<");
        da_add(sizeof(char),str,"<");
        break;
    case REDIR_HERE_STRING:
        da_add(sizeof(char),str,"<");
        da_add(sizeof(char),str,"<");
        da_add(sizeof(char),str,"<");
        break;
    case REDIR_CLOSE:
        da_add(sizeof(char),str,">");
        da_add(sizeof(char),str,"&");
        da_add(sizeof(char),str,"-");
        break;
    }

    if(r->_2){
        for(size_t i=0;r->_2[i];i++){
            da_add(sizeof(char),str,&r->_2[i]);
        }
    }
}

char *restore_cmd(command_t *cmds,size_t size){
    da_str tmp;
    da_init(&tmp);

    int ret_flg=0;

    for(size_t i=0;i<size;i++){
        command_t *now=&cmds[i];

        for(size_t j=0;j<now->cmdsn;j++){
            int *r=now->cmds[j];
            if(*r!=CMD_VAR){
                continue;
            }
            command_var_t *v=(void*)r;
            if(v->umask&(VAR_ARRAY|VAR_INT|VAR_FUNC)){
                continue;
            }
            size_t len=strlen(v->var);
            for(size_t k=0;k<len;k++){
                da_add(sizeof(char),&tmp,&v->var[k]);
            }
        }

        for(size_t j=0;j<now->argvn&&now->argv[j];j++){
            size_t len=strlen(now->argv[j]);
            for(size_t k=0;k<len;k++){
                da_add(sizeof(char),&tmp,&now->argv[j][k]);
            }
            if(now->argv[j+1]!=NULL){
                da_add(sizeof(char),&tmp," ");
            }
        }

        if(now->cmdsn){
            da_add(sizeof(char),&tmp," ");
        }
        for(size_t j=0;j<now->cmdsn;j++){
            int *r=now->cmds[j];
            size_t last_len=tmp.size;
            switch(*r){
            case CMD_BG_END:
                ret_flg=1;
                break;
            case CMD_PIPE:
                da_add(sizeof(char),&tmp,"|");
                break;
            case CMD_REDIR:
                restore_cmd_redir(&tmp,(void*)r);
                break;
            case CMD_AND:
                da_add(sizeof(char),&tmp,"&");
                da_add(sizeof(char),&tmp,"&");
                break;
            case CMD_BG:
                da_add(sizeof(char),&tmp,"&");
                break;
            case CMD_OR:
                da_add(sizeof(char),&tmp,"|");
                da_add(sizeof(char),&tmp,"|");
                break;
            }
            if(*r<0){
                continue;
            }
            if(last_len<tmp.size&&(j<now->cmdsn-1||i<size-1)){
                da_add(sizeof(char),&tmp," ");
            }
        }

        if(ret_flg){
            break;
        }
    }
    da_add(sizeof(char),&tmp,"");
    return tmp.arr;
}

int add_job(char *name,int pid,int stat){
    size_t num=job.size?job.arr[job.size-1].num+1:1;
    job_t j={.name=name,.pid=pid,.stat=stat,.num=num};
    da_add(sizeof(job_t),&job,&j);

    return job.size-1;
}
size_t find_job_pid(int pid){
    if(pid<=0){
        return -1;
    }
    for(size_t i=0;i<job.size;i++){
        if(job.arr[i].pid==pid){
            return i;
        }
    }
    return -1;
}
size_t find_job_num(size_t num){
    if(num<=0){
        return -1;
    }
    for(size_t i=0;i<job.size;i++){
        if(job.arr[i].num==num){
            return i;
        }
    }
    return -1;
}
size_t get_job_num(const char *str){
    if(!job.size){
        return -1;
    }
    unsigned num=0;
    if(str[0]!='%'){
        return -1;
    }
    if(str[1]=='+'){
        num=job.size-1;
    }else if(str[1]=='-'){
        if(job.size>1){
            num=job.arr[job.size-2].num;
        }else{
            num=job.arr[job.size-1].num;
        }
    }else{
        num_t r=cmd_str_to_num(str+1);
        if(r.is_negative||r.unexcepted_char){
            return -1;
        }
        num=r.num;
    }
    if(num==(unsigned)-1){
        return -1;
    }
    return num;
}
int del_job_pid(int pid){
    if(pid<=0){
        return 127;
    }
    size_t i=0;
    for(i=0;i<job.size;i++){
        if(job.arr[i].pid==pid){
            break;
        }
    }
    if(i==job.size){
        return 127;
    }
    free(job.arr[i].name);
    if(i<job.size-1){
        memmove(job.arr+i,job.arr+i+1,sizeof(job_t));
    }
    da_pop(sizeof(job_t),&job);
    return 0;
}

int deal_jobmsg(void){
    size_t i=0;
    while(i<job.size){
        if(job.arr[i].stat==JOB_OUT_STOPPED){
            struct sigaction sa;
            sa.sa_handler=SIG_IGN;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags=SA_RESTART;
            sigaction(SIGTTOU,&sa,NULL);

            tcsetpgrp(STDIN_FILENO,job.arr[i].pid);
            now_pid=job.arr[i].pid;
            kill(job.arr[i].pid,SIGCONT);
            int status=0;
            size_t j=0;
            while(j<MAX_JOB_OUT_TIMES&&waitpid(job.arr[i].pid,&status,WUNTRACED|WNOHANG)<=0){
                usleep(10);
                j++;
            }

            tcsetpgrp(STDIN_FILENO,getpgrp());
            if(status){
                deal_job_status(&job.arr[i],status);
            }
            for(size_t j=0;j<jobmsgsiz;j++){
                if(jobmsg[j].stat==JOB_OUT_STOPPED&&jobmsg[j].pid==job.arr[i].pid){
                    jobmsg[j].pid=0;
                }
            }
            now_pid=0;
            sa.sa_handler=SIG_DFL;
            sigaction(SIGTTOU,&sa,NULL);
        }
        i++;
    }
    for(size_t i=0;i<jobmsgsiz;i++){
        if(jobmsg[i].pid==0){
            continue;
        }
        size_t k=find_job_pid(jobmsg[i].pid);
        echo_to_buf("[",1);
        echo_unsigned_to_buf(job.arr[k].num);
        echo_to_buf("]",1);

        switch(jobmsg[i].stat){
        case JOB_RUNNING:
            echo_to_buf(" ",1);
            echo_unsigned_to_buf(job.arr[k].pid);
            break;
        case JOB_FINISHED:
            if(k==job.size-1){
                echo_to_buf("+",1);
            }else if(job.size>1&&k==job.size-2){
                echo_to_buf("-",1);
            }else{
                echo_to_buf(" ",1);
            }
            echo_to_buf(" ",1);
            echo_to_buf("Done\t\t\t",7);
            echo_to_buf(job.arr[k].name,strlen(job.arr[k].name));

            free(job.arr[k].name);
            if(k<job.size-1){
                memmove(job.arr+k,job.arr+k+1,sizeof(job_t));
            }
            da_pop(sizeof(job_t),&job);
            break;
        case JOB_STOPPED:
        case JOB_IN_STOPPED:
        case JOB_OUT_STOPPED:
            if(k==job.size-1){
                echo_to_buf("+",1);
            }else if(job.size>1&&k==job.size-2){
                echo_to_buf("-",1);
            }else{
                echo_to_buf(" ",1);
            }
            echo_to_buf(" ",1);
            echo_to_buf("Stopped",7);
            if(jobmsg[i].stat==JOB_IN_STOPPED){
                echo_to_buf(" (tty input)\t\t",14);
            }else if(jobmsg[i].stat==JOB_OUT_STOPPED){
                echo_to_buf(" (tty output)\t\t",15);
            }else{
                echo_to_buf("\t\t\t",3);
            }
            echo_to_buf(job.arr[k].name,strlen(job.arr[k].name));
            break;
        }

        echo_to_buf("\n",1);
        echo_buf_to_fd(STDERR_FILENO);
    }
    jobmsgsiz=0;

    return 0;
}

inline static void deal_job_status(job_t *job,int status){
    if(WIFSTOPPED(status)){
        if(WSTOPSIG(status)==SIGTTOU){
            if(job->stat==JOB_OUT_STOPPED){
                return ;
            }
            job->stat=JOB_OUT_STOPPED;
        }else if(WSTOPSIG(status)==SIGTTIN){
            job->stat=JOB_IN_STOPPED;
        }else{
            job->stat=JOB_STOPPED;
        }
        if(jobmsgsiz<JOB_MSG_SIZE){
            jobmsg[jobmsgsiz++]=(jobmsg_t){.pid=job->pid,.stat=job->stat};
        }
        return ;
    }
    if(WIFEXITED(status)){
        g_ret=WEXITSTATUS(status);
        job->stat=JOB_FINISHED;
        if(jobmsgsiz<JOB_MSG_SIZE){
            jobmsg[jobmsgsiz++]=(jobmsg_t){.pid=job->pid,.stat=JOB_FINISHED};
        }
    }
}

void sig_int_handler(int sig){
    if(now_pid<0){
        exit(127);
    }else if(now_pid==0){
        return ;
    }
    kill(now_pid,SIGINT);
    output("\n",1);
    now_pid=0;
    if(is_child){
        exit(127);
    }
}
void sig_chld_handler(int sig){
    int status=0;
    for(size_t i=0;i<job.size;i++){
        if(job.arr[i].pid==now_pid){
            continue;
        }
        if(waitpid(job.arr[i].pid,&status,WNOHANG|WUNTRACED)<=0){
            continue;
        }
        deal_job_status(&job.arr[i],status);
    }
}
void sig_tstp_handler(int sig){
    if(now_pid<=0&&!is_child){
        return ;
    }
    if(is_child){
        if(now_pid>0){
            kill(now_pid,SIGSTOP);
        }
        raise(SIGSTOP);
        return ;
    }
    kill(now_pid,SIGTSTP);
}
void sig_cont_handler(int sig){
    if(now_pid<=0){
        return ;
    }
    kill(now_pid,SIGCONT);
    if(is_child){
        return ;
    }
    for(size_t i=0;i<job.size;i++){
        if(job.arr[i].stat==JOB_RUNNING){
            kill(job.arr[i].pid,SIGCONT);
        }
    }
}




size_t cmd_unsigned_to_str(char *str,size_t size,size_t num){
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

char *cmd_num_to_str(size_t num,int is_negative){
    size_t len=0;
    size_t k=num;
    do{
        k/=10;
        len++;
    }while(k);
    len+=(is_negative?1:0);
    char *ret=malloc(sizeof(char)*(len+1));
    size_t i=0;
    if(is_negative){
        ret[i++]='-';
    }
    for(;i<len;i++){
        ret[len-i-1]=num%10+'0';
        num/=10;
    }
    ret[len]='\0';
    return ret;
}

num_t cmd_str_to_num(const char *str){
    num_t r={};
    unsigned i=0;
    if(str[0]=='-'){
        r.is_negative=1;
    }
    while(str[i]){
        if(str[i]<'0'||str[i]>'9'){
            r.unexcepted_char=str[i];
            break;
        }
        r.num*=10;
        r.num=str[i]-'0';
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
    const char *path=gets_var("PATH");
    if(!path){
        return NULL;
    }
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

int is_redirector(const char *cmd,size_t *inter,int *a){
    const struct{
        char key[4];
        int value;
    }redirector[]={
        {.key="<"  ,.value=REDIR_IN           }, 
        {.key=">"  ,.value=REDIR_OUT          },
        {.key=">>" ,.value=REDIR_OUT_ADD      },
        {.key=">&" ,.value=REDIR_DUP          },
        {.key="<&" ,.value=REDIR_DUP          },
        {.key="<<" ,.value=REDIR_HERE_DOCUMENT},
        {.key="<<<",.value=REDIR_HERE_STRING  },
        {.key=">&-",.value=REDIR_CLOSE        },
        {.key="<&-",.value=REDIR_CLOSE        }
    };
    size_t redir_size=sizeof(redirector)/sizeof(redirector[0]);

    size_t k=0;
    int ta=0;
    while(cmd[k]>='0'&&cmd[k]<='9'){
        ta*=10;
        ta+=cmd[k]-'0';
        k++;
    }

    size_t i=0,j=0;
    int v=0;
    while(cmd[i+k]!='\0'){
        while(j<redir_size&&redirector[j].key[i]!=cmd[i+k]){
            j++;
        }
        if(j==redir_size){
            break;
        }
        v=redirector[j].value;
        i++;
    }
    if(inter){
        *inter=k+i;
    }
    if(a){
        if(k){
            *a=ta;
        }else{
            *a=-1;
        }
    }
    return v;
}


int cmd_execvpe(const char *file, char *const argv[],char *const envp[]){
    const char *p=file_is_exist(file,F_OK|X_OK,1);
    if(p){
        return execve(p,argv,envp);
    }
    return -2;
}


int set_terminal_echo(int enable){
    if(is_script||is_child){
        return 0;
    }

    static struct termios original_termios;
    static int is_saved=0;
    struct termios new_termios;

    if(!is_saved){
        if(tcgetattr(STDIN_FILENO,&original_termios)==-1){
            return 1;
        }
        is_saved=1;
    }

    new_termios=original_termios;

    if(!enable){
        new_termios.c_lflag&=~(ECHO|ICANON|ECHOE|ECHOK|ECHONL);
        #ifdef TERM_TOSTOP
        new_termios.c_lflag|=TOSTOP;
        #endif
        new_termios.c_cc[VMIN]=1;
        new_termios.c_cc[VTIME]=0;
    }

    if(tcsetattr(STDIN_FILENO,TCSANOW,&new_termios)==-1){
        return 1;
    }

    return 0;
}

void set_signal_handler(int enable){
    if(is_script){
        return ;
    }
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags=0;

    if(enable){
        sa.sa_handler=SIG_DFL;
        sigaction(SIGINT,&sa,NULL);
        sigaction(SIGCHLD,&sa,NULL);
        sigaction(SIGTSTP,&sa,NULL);
        sigaction(SIGCONT,&sa,NULL);
        return ;
    }

    sa.sa_flags=SA_RESTART;

    sa.sa_handler=sig_int_handler;
    sigaction(SIGINT,&sa,NULL);

    sa.sa_handler=sig_chld_handler;
    sigaction(SIGCHLD,&sa,NULL);

    sa.sa_handler=sig_tstp_handler;
    sigaction(SIGTSTP,&sa,NULL);

    sa.sa_handler=sig_cont_handler;
    sigaction(SIGCONT,&sa,NULL);
}

void child_clear(void){
    for(size_t i=0;i<history.size;i++){
        free(history.arr[i]);
    }
    da_clear(&history);
    for(size_t i=0;i<job.size;i++){
        free(job.arr[i].name);
    }
    da_clear(&job);
    da_clear(&buffer);
    free(g_argv_var);
}


int sh_cd(char *const *argv){
    int r=1;
    const char *p=NULL;
    int l=1;

    size_t i=1;
    while(argv[i]){
        if(argv[i][0]!='-'){
            p=argv[i];
            goto L;
        }
        if(!strcmp(argv[i],"--help")){
            return 2;
        }
        size_t j=1;
        while(argv[i][j]){
            if(argv[i][j]=='P'){
                l=0;
            }else if(argv[i][j]=='L'){
                l=1;
            }else{
                write(STDERR_FILENO,"cd: invalid option\n",19);
                return 2;
            }
            j++;
        }
        i++;
    }
    p=argv[i];
    L:
    if(!argv[i]){
        p=gets_var("HOME");
        if(!p){
            p="/home";
        }
    }


    if(argv[i]&&argv[i+1]){
        write(STDERR_FILENO,"cd: too many arguments\n",23);
        return 2;
    }
    r=chdir(p);
    if(r){
        write(STDERR_FILENO,"cd: ",4);
        write(STDERR_FILENO,argv[1],strlen(argv[1]));
        write(STDERR_FILENO,": no such file or directory\n",28);
        return 1;
    }
    if(l){
        pathbuf[0]='\0';
        strcpy(pathbuf,"PWD=");
        strcat(pathbuf,p);
    }else{
        getcwd(pathbuf,PATH_BUF_SIZE);
        size_t len=strlen(pathbuf);
        memmove(pathbuf+4,pathbuf,len);
        memcpy(pathbuf,"PWD=",4);
        pathbuf[len+4]='\0';
    }
    set_var(pathbuf,VAR_EXPORT);
    return 0;
}
int sh_pwd(char *const *argv){
    size_t i=1;
    int l=0;
    while(argv[i]){
        if(argv[i][0]!='-'){
            goto L;
        }
        if(!strcmp(argv[i],"--help")){
            return 2;
        }
        size_t j=1;
        while(argv[i][j]){
            if(argv[i][j]=='P'){
                l=0;
            }else if(argv[i][j]=='L'){
                l=1;
            }else{
                write(STDERR_FILENO,"pwd: invalid option\n",20);
                return 2;
            }
            j++;
        }
        i++;
    }
    L:
    if(l){
        const char *r=gets_var("PWD");
        if(!r){
            write(STDERR_FILENO,"pwd: failed\n",12);
            return 1;
        }
        memcpy(pathbuf,r,strlen(r)+1);
    }else{
        char *r=getcwd(pathbuf,PATH_BUF_SIZE);
        if(r!=pathbuf){
            write(STDERR_FILENO,"pwd: failed\n",12);
            return 1;
        }
    }
    write(STDOUT_FILENO,pathbuf,strlen(pathbuf));
    write(STDOUT_FILENO,"\n",1);
    return 0;
}
int sh_history(char *const *argv){
    size_t i=1;
    while(argv[i]){
        if(argv[i][0]!='-'){
            write(STDERR_FILENO,"cd: too many arguments\n",23);
        }
        if(!strcmp(argv[i],"--help")){
            return 2;
        }
        size_t j=1;
        while(argv[i][j]){
            if(argv[i][j]=='c'){
                for(size_t i=0;i<history.size;i++){
                    free(history.arr[i]);
                }
                da_clear(&history);
                history_pos=0;
            }else{
                write(STDERR_FILENO,"history: invalid option\n",19);
                return 2;
            }
            j++;
        }
        i++;
    }

    i=0;
    size_t len=0;
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
        write(STDERR_FILENO,"export: ",8);
        write(STDERR_FILENO,argv[1],strlen(argv[1]));
        write(STDERR_FILENO,"\n",1);
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
    int ret=is_variable(argv[1]);
    if(!ret){
        write(STDERR_FILENO,"readonly: ",8);
        write(STDERR_FILENO,argv[1],strlen(argv[1]));
        write(STDERR_FILENO,"\n",1);
        return 127;
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
    size_t last_num=0;
    size_t i=0;
    for(size_t j=0;j<job.size;j++){
        i=(size_t)-1;
        for(size_t k=0;k<job.size;k++){
            if(last_num<job.arr[k].num&&(i==(size_t)-1||job.arr[k].num<job.arr[i].num)){
                i=k;
            }
        }
        last_num=job.arr[i].num;
        echo_to_buf("[",1);
        echo_unsigned_to_buf(job.arr[i].num);
        echo_to_buf("]",1);
        if(i==job.size-1){
            echo_to_buf("+  ",3);
        }else if(job.size>1&&i==job.size-2){
            echo_to_buf("-  ",3);
        }else{
            echo_to_buf("   ",3);
        }
        if(job.arr[i].stat==JOB_RUNNING){
            echo_to_buf("Running\t\t\t",10);
        }else if(job.arr[i].stat==JOB_STOPPED){
            echo_to_buf("Stopped\t\t\t",10);
        }else if(job.arr[i].stat==JOB_IN_STOPPED){
            echo_to_buf("Stopped (tty input)\t\t\t",22);
        }else if(job.arr[i].stat==JOB_OUT_STOPPED){
            echo_to_buf("Stopped (tty output)\t\t\t",23);
        }
        echo_to_buf(job.arr[i].name,strlen(job.arr[i].name));
        echo_to_buf("\n",1);
        echo_buf_to_fd(STDOUT_FILENO);
    }
    return 0;
}
int sh_fg(char *const *argv){
    size_t num=0;
    if(!job.size){
        return 127;
    }
    if(!argv[1]){
        num=job.arr[job.size-1].num;
    }else{
        num=get_job_num(argv[1]);
        if(num==(size_t)-1){
            return 127;
        }
    }

    size_t i=find_job_num(num);
    if(i==(size_t)-1){
        return 127;
    }
    job_t j=job.arr[i];
    if(i<job.size-1){
        memmove(job.arr+i,job.arr+i+1,sizeof(job_t));
    }
    da_pop(sizeof(job_t),&job);
    now_pid=j.pid;

    struct sigaction sa;
    sa.sa_handler=SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags=SA_RESTART;
    sigaction(SIGTTOU,&sa,NULL);

    tcsetpgrp(STDIN_FILENO,j.pid);
    kill(j.pid,SIGCONT);
    int status;
    waitpid(j.pid,&status,WUNTRACED);

    tcsetpgrp(STDIN_FILENO,getpgrp());

    deal_job_status(&j,status);

    sa.sa_handler=SIG_DFL;
    sigaction(SIGTTOU,&sa,NULL);
    da_add(sizeof(job_t),&job,&j);
    return 0;
}
int sh_bg(char *const *argv){
    size_t num=0;
    if(!job.size){
        return 127;
    }
    if(!argv[1]){
        num=job.arr[job.size-1].num;
    }

    size_t k=1;
    while(argv[k]){
        num=get_job_num(argv[1]);
        if(num==(size_t)-1){
            return 127;
        }

        size_t i=find_job_num(num);
        if(i==(size_t)-1){
            return 127;
        }
        job_t *j=&job.arr[i];

        kill(j->pid,SIGCONT);
        j->stat=JOB_RUNNING;

        k++;
    }
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
    if(!argv[1]){
        for(unsigned i=0;i<alias.size;i++){
            write(STDOUT_FILENO,"alias ",6);
            write(STDOUT_FILENO,alias.arr[i].var,strlen(alias.arr[i].var));
            write(STDOUT_FILENO,"\n",1);
        }
        return 0;
    }
    int ret=is_variable(argv[1]);
    if(!ret){
        write(STDERR_FILENO,"alias: ",7);
        write(STDERR_FILENO,argv[1],strlen(argv[1]));
        write(STDERR_FILENO,"\n",1);
        return 127;
    }
    return set_alias(argv[1])?127:0;
}
int sh_unalias(char *const *argv){
    if(!argv[1]){
        return 127;
    }
    return unset_alias(argv[1])?127:0;
}
int sh_type(char *const *argv){
    return 0;
}


