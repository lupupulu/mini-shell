#include "mnsh.h"
#include <unistd.h>
#include <memory.h>
#include <string.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <locale.h>

int execute_shebang(int fd,char* const* argv);

int execute_command_parent(command_t *cmd);
int execute_command_child(command_t *cmd);

#define EXE_START  0b0001
#define EXE_PARENT 0b0010
#define EXE_CLEAR  0b0100

#define EXE_PIPE        0b00000001
#define EXE_WRONG_FILE  0b00000010
#define EXE_WRONG_PIPE  0b00000100
#define EXE_AND         0b00001000
#define EXE_OR          0b00010000
#define EXE_ADD_BG      0b00100000
#define EXE_DEL_BG      0b01000000
#define EXE_WRONG_REDIR 0b10000000

int exe_parse_redirector(int st,command_redir_t *redir);
int exe_parse_pipe(int cmd,int st,int *pipes,int *prev_pipe);
int exe_parse_cmd(command_t *cmd,int st);

int exe_bg_start(command_t *cmd);
int exe_bg_end(command_t *cmd);

int insert_to_buffer(const char *str,size_t len,size_t pos);
int delete_to_buffer(size_t len,size_t pos);

char parse_variable(command_t *now,da_str *buf,const char *str,size_t *inter);
char parse_single_quote(da_str *buf,const char *str,size_t *inter);
char parse_quote(command_t *now,da_str *buf,const char *str,size_t *inter);
char parse_home(da_str *buf,const char *str,size_t *inter);
char parse_array(command_var_t *r,const char *str,size_t *inter);
char parse_cmd_substitution(command_t *now,da_str *buf,const char *str,size_t *inter);
char parse_substitution_command(command_t *now,da_str *buf,const char *str);

#define IS_PARSED               0b00000001
#define PARSE_IS_VARIABLE       0b00000010
#define PARSE_IS_CONTINUE       0b00000100
#define PARSE_IS_BACKGROUND     0b00001000
#define PARSE_PARENTHESES_START 0b00010000
#define PARSE_PARENTHESES_END   0b00100000
#define PARSE_IS_ARRAY          0b01000000
#define PARSE_SUBPRASE_END      0b10000000

int parse_item(command_t *now,da_str *buf,const char *str,size_t *inter);
int parse_command(command_t *now,const char *str,size_t *inter,int flg);
int parse_buffer(da_command *cmds,const char *str,size_t *inter);

int parse_is_continue(da_str *buf);

int execute_command(da_command *p,size_t i);

int main(int argc,char *const *argv,char *const *envi){
    setpgid(0,0);
    char *tmp=NULL;
    da_add(sizeof(char*),&env,&tmp);
    unsigned i=0;
    while(envi[i]){
        set_var(envi[i],VAR_EXPORT);
        i++;
    }

    setlocale(LC_CTYPE,"");


    if(argc>=2){
        const char *p=file_is_exist(argv[1],F_OK|X_OK,1);
        if(!p){
            write(STDERR_FILENO,argv[1],strlen(argv[1]));
            write(STDERR_FILENO,": command not found\n",20);
            return 127;
        }
        char c[4]={};
        int fd=open(p,O_RDONLY);
        read(fd,c,2);
        if(!strcmp(c,"#!")){
            execute_shebang(fd,argv);
            return 127;
        }
        lseek(fd,0,SEEK_SET);
        is_script=1;
        dup2(fd,STDIN_FILENO);
        g_argc=argc-2;
        g_argv=&argv[1];
    }else{
        g_argc=0;
        g_argv=argv;
    }
    g_argv_var=malloc(sizeof(size_t)+sizeof(char*)*argc);
    var_arr_t(char*) *argv_arr=g_argv_var;
    argv_arr->size=g_argc;
    memcpy(argv_arr->data,&g_argv[1],sizeof(char*)*g_argc);

    g_pid=getpid();
    
    if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO)||!isatty(STDERR_FILENO)){
        is_script=1;
    }
    set_signal_handler(0);

    if(!is_script){
        set_var("PS1=$ ",0);
        set_var("PS2=> ",0);
        set_var("PS4=+ ",0);
        tcsetpgrp(STDIN_FILENO,getpgrp());
    }
    const char *psn="PS1";


    da_str tmp_buffer;
    da_init(&tmp_buffer);

    da_command cmds;
    da_init(&cmds);

    while(1){
        deal_jobmsg();

        const char *ps=NULL;
        if(!is_script){
            ps=gets_var(psn);
        }
        da_init(&buffer);
        int r=input_readline(ps);

        deal_jobmsg();

        size_t i=0;
        int ret=0;

        switch(r){
        case 127:
            return 127;
        case -1:
            set_signal_handler(1);
            printf("\nexit\n");
            return 0;
        case 2:
        case 0:
            if(!buffer.arr){
                if(r==2){
                    return g_ret;
                }
                continue;
            }

            if(tmp_buffer.size){
                da_resize(sizeof(char),&tmp_buffer,tmp_buffer.size+buffer.size);
                memcpy(tmp_buffer.arr+tmp_buffer.size,buffer.arr,buffer.size);
                tmp_buffer.size+=buffer.size;
                da_clear(&buffer);
                memcpy(&buffer,&tmp_buffer,sizeof(da_str));
                da_init(&tmp_buffer);

                da_pop(sizeof(char*),&history);
            }
            da_add(sizeof(char),&buffer,"");
            buffer.size--;
            da_add(sizeof(char*),&history,&buffer.arr);
            ret=parse_is_continue(&buffer);

            if(ret>0){
                da_add(sizeof(char),&buffer,"\n");
                memcpy(&tmp_buffer,&buffer,sizeof(da_str));
                psn="PS2";
                if(r==2){
                    return 0;
                }
                continue;
            }else if(ret<0){
                i=0;
                psn="PS1";
                continue;
            }

            parse_buffer(&cmds,buffer.arr,&i);
            deal_jobmsg();
            da_clear(&cmds);
            i=0;
            psn="PS1";

            if(r==2){
                return 0;
            }
            break;
        }
    }

    set_signal_handler(1);
    printf("exit\n");

    return 0;
}

int insert_to_buffer(const char *str,size_t len,size_t pos){
    da_resize(sizeof(char),&buffer,buffer.size+len);
    memmove(buffer.arr+pos+len,buffer.arr+pos,sizeof(char)*(buffer.size-pos));
    memcpy(buffer.arr+pos,str,len);
    buffer.size+=len;
    return 0;
}

int delete_to_buffer(size_t len,size_t pos){
    memmove(buffer.arr+pos,buffer.arr+pos+len,sizeof(char)*(buffer.size-pos-len));
    buffer.size-=len;
    return 0;
}


int execute_shebang(int fd,char* const* argv){
    darray_t(char*) cmds;
    da_init(&cmds);
    char c=0;
    int r=read(fd,&c,1);
    while(c!='\0'&&c!='\n'&&r){
        while(c==' '){
            r=read(fd,&c,1);
        }
        while(c!=' '&&c!='\0'&&c!='\n'){
            da_add(sizeof(char),&buffer,&c);
            r=read(fd,&c,1);
        }
        da_add(sizeof(char),&buffer,"");
        da_add(sizeof(char*),&cmds,&buffer.arr);
        da_init(&buffer);
        if(c=='\0'||c=='\n'){
            break;
        }
    }
    close(fd);
    size_t i=1;
    do{
        da_add(sizeof(char*),&cmds,&argv[i]);
    }while(argv[i++]);
    execvp(cmds.arr[0],cmds.arr);
    return -1;
}


int parse_is_continue(da_str *buf){
    int quote=0,subparse=0;
    int parenthese=0,sqr_bracket=0;
    for(size_t i=0;i<buf->size;i++){
        switch(buf->arr[i]){
        case '\"':
            quote=!quote;
            break;
        case '`':
            subparse=!subparse;
            break;
        case '\'':
            if(quote){
                break;
            }
            i++;
            for(;i<buf->size;i++){
                if(buf->arr[i]=='\''){
                    goto L1;
                }
            }
            return 1;
        case '(':
            parenthese++;
            break;
        case ')':
            parenthese--;
            break;
        case '[':
            sqr_bracket++;
            break;
        case ']':
            sqr_bracket--;
            break;
        case '\\':
            i++;
            break;
        case '#':
            goto L2;
        }
        L1:;
    }
    if(buf->arr[buf->size-1]=='\\'){
        da_pop(sizeof(char),buf);
        return 1;
    }
    L2:
    if(sqr_bracket<0||parenthese<0){
        return -1;
    }
    if(sqr_bracket>0||parenthese>0||quote||subparse){
        return 1;
    }
    return 0;
}


int quote_flg=0;
int cmd_substitution_cnt=0;

char parse_variable(command_t *now,da_str *buf,const char *str,size_t *inter){
    if(str[*inter+1]=='('){
        (*inter)+=2;
        cmd_substitution_cnt++;
        return parse_cmd_substitution(now,buf,str,inter);
    }

    size_t i=*inter;

    da_str tmp;
    da_init(&tmp);
    da_str num;
    da_init(&num);
    if(str[i+1]=='{'){
        i+=2;
        while(str[i]!='\0'&&str[i]!='}'&&str[i]!='['){
            da_add(sizeof(char),&tmp,&str[i]);
            i++;
        }
        if(str[i]=='['){
            i++;
            while(str[i]!='\0'&&str[i]!='}'&&str[i]!=']'){
                da_add(sizeof(char),&num,&str[i]);
                i++;
            }
            if(str[i]=='}'||str[i]=='\0'){
                da_clear(&tmp);
                da_clear(&num);
                return 0;
            }
            i++;
            da_add(sizeof(char),&num,"");
        }
        if(str[i]=='\0'){
            da_clear(&tmp);
            return '}';
        }
        i++;
        da_add(sizeof(char),&tmp,"");
    }else if(IS_SPECIAL_VARIABLE(str[i+1])){
        i++;
        da_add(sizeof(char),&tmp,&str[i]);
        da_add(sizeof(char),&tmp,"");
        i++;
    }else{
        i++;
        while(str[i]!='\0'&&IS_LEGAL(str[i])){
            da_add(sizeof(char),&tmp,&str[i]);
            i++;
        }
        da_add(sizeof(char),&tmp,"");
    }
    *inter=i;

    var_t var=get_var(tmp.arr);
    da_clear(&tmp);
    if(!var.value){
        da_clear(&num);
        return 0;
    }

    size_t j=0;

    do{
        if(j){
            if(quote_flg&&var.umask&VAR_EXPAND_IN_QUOTE){
                cm_add_item(now,buf->arr);
                da_fake_clear(buf);
            }else{
                da_add(sizeof(char),buf," ");
            }
        }
        void *tmpp=var.value;
        char *rstr=NULL;
        int need_free=0;

        if(var.umask&VAR_ARRAY){
            if(!num.size){
                tmpp=var.umask&VAR_INT?
                    (var.value+sizeof(size_t)+j*sizeof(size_t)):*(char**)(var.value+sizeof(size_t)+j*sizeof(char*));
                goto L;
            }
            num_t r=cmd_str_to_num(num.arr);
            var_arr_t(char*) *arr=var.value;
            if(r.is_negative||r.unexcepted_char||r.num>=arr->size){
                da_clear(&num);
                return 0;
            }
            tmpp=arr->data[r.num];
        }
        L:
        if(var.umask&VAR_INT){
            rstr=cmd_num_to_str((size_t)tmpp,0);
            need_free=1;
        }else if(var.umask&VAR_FUNC){
            var_func_t *func=tmpp;
            rstr=restore_cmd(func->value,func->size);
            need_free=1;
        }else{
            rstr=tmpp;
        }

        if(!rstr){
            da_clear(&num);
            return 0;
        }
        size_t k=0;
        while(rstr[k]!='\0'){
            da_add(sizeof(char),buf,&rstr[k]);
            k++;
        }

        if(need_free){
            free(rstr);
        }
        j++;

    }while(var.umask&VAR_EXPAND&&var.umask&VAR_ARRAY&&!num.size&&j<*(size_t*)var.value);

    da_clear(&num);
    return 0;
}

char parse_single_quote(da_str *buf,const char *str,size_t *inter){
    size_t i=*inter;
    if(!quote_flg){
        quote_flg='\'';
        i++;
    }
    while(str[i]!='\0'&&str[i]!='\''){
        da_add(sizeof(char),buf,&str[i]);
        i++;
    }
    if(str[i]=='\0'){
        return '\'';
    }
    quote_flg=0;
    i++;
    *inter=i;
    return 0;
}

char parse_quote(command_t *now,da_str *buf,const char *str,size_t *inter){
    size_t i=*inter;
    if(!quote_flg){
        quote_flg='\"';
        i++;
    }
    while(str[i]!='\0'&&str[i]!='\"'){
        switch(str[i]){
        case '$':
            parse_variable(now,buf,str,&i);
            break;
        case '`':
            cmd_substitution_cnt++;
            i++;
            parse_cmd_substitution(now,buf,str,&i);
            break;
        case '\\':
            if(!IS_LEGAL(str[i])){
                i++;
            }
        default:
            da_add(sizeof(char),buf,&str[i]);
            i++;
            break;
        }
    }
    if(str[i]=='\0'){
        return '\"';
    }
    quote_flg=0;
    i++;
    *inter=i;
    return 0;
}

char parse_home(da_str *buf,const char *str,size_t *inter){
    size_t i=*inter;
    i++;
    const char *home=gets_var("HOME");
    if(!home){
        home="/home";
    }
    size_t j=0;
    while(home[j]){
        da_add(sizeof(char),buf,&home[j]);
        j++;
    }
    *inter=i;
    return 0;
}

char parse_array(command_var_t *r,const char *str,size_t *inter){
    var_arr_t(char*) *arr=malloc(sizeof(size_t));
    arr->size=0;
    
    size_t i=*inter;
    i++;
    command_t cmd;
    cm_init(&cmd);
    da_str buf;
    da_init(&buf);
    while(str[i]==' ')i++;
    while(1){
        switch(str[i]){
        case '$':
            parse_variable(&cmd,&buf,str,&i);
            break;
        case '\"':
            parse_quote(&cmd,&buf,str,&i);
            break;
        case '\'':
            parse_single_quote(&buf,str,&i);
            break;
        case '~':
            if((!i||str[i-1]==' ')&&(!str[i+1]||str[i+1]=='/'||str[i+1]==' ')){
                parse_home(&buf,str,&i);
                break;
            }
            da_add(sizeof(char),&buf,&str[i]);
            i++;
            break;
        case '\\':
            if(!IS_LEGAL(str[i])){
                i++;
            }
        case '\0':
        case ')':
            break;
        default:
            da_add(sizeof(char),&buf,&str[i]);
            i++;
            break;
        }
        if(str[i]==' '||str[i]=='\0'||str[i]==')'){
            if(cmd.argvn){
                arr=realloc(arr,(arr->size+cmd.argvn)*sizeof(char*)+sizeof(size_t));
                memcpy(&arr->data[arr->size],&cmd.argv,sizeof(char*)*cmd.argvn);
                arr->size+=cmd.argvn;
                cm_clear(&cmd);
                cm_init(&cmd);
            }else{
                da_add(sizeof(char),&buf,"");
                arr=realloc(arr,(arr->size+1)*sizeof(char*)+sizeof(size_t));
                arr->data[arr->size]=buf.arr;
                arr->size++;
            }

            while(str[i]==' ')i++;
            if(str[i]=='\0'||str[i]==')'){
                break;
            }
            da_init(&buf);
        }
    }
    i++;
    *inter=i;
    size_t len=strlen(r->var),arrsiz=VAR_ARR_SIZE(char*,arr);
    r->var=realloc(r->var,(len+1)*sizeof(char)+arrsiz);
    memcpy(r->var+len,arr,arrsiz);
    return 0;
}

char parse_cmd_substitution(command_t *now,da_str *buf,const char *str,size_t *inter){
    int pipes[2]={-1,-1};
    int savefd=-1;
    if(pipe(pipes)){
        return 0;
    }
    savefd=dup(STDOUT_FILENO);
    
    dup2(pipes[1],STDOUT_FILENO);
    is_script=1;

    da_command cmds;
    da_init(&cmds);

    parse_buffer(&cmds,str,inter);
    da_clear(&cmds);

    dup2(savefd,STDOUT_FILENO);
    close(savefd);
    close(pipes[1]);
    pipes[1]=-1;
    savefd=-1;

    char c;
    da_str *bufp=buf;
    if(!quote_flg){
        bufp=malloc(sizeof(da_str));
        da_init(bufp);
    }
    while(read(pipes[0],&c,sizeof(char))){
        da_add(sizeof(char),bufp,&c);
    }
    if(bufp->size&&(bufp->arr[bufp->size-1]=='\n'||bufp->arr[bufp->size-1]==' ')){
        da_pop(sizeof(char),bufp);
    }

    is_script=0;
    close(pipes[0]);
    pipes[0]=-1;

    if(quote_flg){
        goto RET;
    }

    da_add(sizeof(char),bufp,"");
    parse_substitution_command(now,buf,bufp->arr);
    da_clear(bufp);

    RET:
    cmd_substitution_cnt--;

    return 0;
}

char parse_substitution_command(command_t *now,da_str *buf,const char *str){
    size_t i=0;
    while(str[i]==' '||str[i]=='\n'){
        i++;
    }
    while(str[i]){
        switch(str[i]){
        case '$':
            parse_variable(now,buf,str,&i);
            break;
        case '`':
            cmd_substitution_cnt++;
            i++;
            parse_cmd_substitution(now,buf,str,&i);
            break;
        case ' ':
        case '\n':
            da_add(sizeof(char),buf," ");
            i++;
            while(str[i]==' '||str[i]=='\n'){
                i++;
            }
            break;
        default:
            da_add(sizeof(char),buf,&str[i]);
            i++;
            break;
        }
    }
    return 0;
}

int parse_item(command_t *now,da_str *buf,const char *str,size_t *inter){
    size_t i=*inter;
    while(str[i]==' '){
        i++;
    }
    if(str[i]=='#'){
        return 0;
    }
    *inter=i;

    int ret=0;
    int redir=0;
    command_var_t array={.type=CMD_VAR,.umask=VAR_ARRAY,.var=NULL};

    while(str[i]!='\0'&&str[i]!=' '){
        size_t j=0;
        switch(str[i]){
        case ';':
            if(i==*inter){
                da_add(sizeof(char),buf,";");
                i++;
            }
            *inter=i;
            return ret;
        case '|':
            if(i==*inter){
                da_add(sizeof(char),buf,"|");
                i++;
                if(str[i]=='&'){
                    da_add(sizeof(char),buf,"&");
                    i++;
                }else if(str[i]=='|'){
                    da_add(sizeof(char),buf,"|");
                    i++;
                }
            }
            *inter=i;
            return ret;
        case '&':
            if(i==*inter){
                da_add(sizeof(char),buf,"&");
                i++;
                if(str[i]=='&'){
                    da_add(sizeof(char),buf,"&");
                    i++;
                }
            }
            *inter=i;
            return ret;
        case '>':
        case '<':
            redir=is_redirector(str+*inter,&j,NULL);
            if(redir){
                for(size_t k=0;k<j;k++){
                    da_add(sizeof(char),buf,&str[*inter+k]);
                }
                *inter+=j;
                return ret;
            }else if(is_redirector(str+i,NULL,NULL)){
                *inter=i;
                return ret;
            }
            while(str[i]=='>'||str[i]=='<'){
                da_add(sizeof(char),buf,&str[i]);
                i++;
            }
            break;
        case '$':
            ret|=IS_PARSED;
            parse_variable(now,buf,str,&i);
            break;
        case '\'':
            ret|=IS_PARSED;
            parse_single_quote(buf,str,&i);
            break;
        case '\"':
            ret|=IS_PARSED;
            parse_quote(now,buf,str,&i);
            break;
        case '(':
            if(ret&PARSE_IS_VARIABLE&&buf->arr[buf->size-1]=='='){
                da_add(sizeof(char),&buf->arr,"");
                array.var=buf->arr;
                parse_array(&array,str,&i);
                cm_add_cmd(now,&array,sizeof(command_var_t));
                ret=PARSE_IS_ARRAY;
                *inter=i;
                return ret;
            }
            i++;
            ret|=PARSE_PARENTHESES_START;
            break;
        case ')':
            ret|=PARSE_PARENTHESES_END;
            if(cmd_substitution_cnt){
                ret|=PARSE_SUBPRASE_END;
            }
            i++;
            *inter=i;
            return ret;
        case '`':
            ret|=IS_PARSED;
            i++;
            if(cmd_substitution_cnt){
                *inter=i;
                ret|=PARSE_SUBPRASE_END;
                return ret;
            }
            cmd_substitution_cnt++;
            parse_cmd_substitution(now,buf,str,&i);
            break;
        case '=':
            if(!(ret&IS_PARSED)){
                ret|=PARSE_IS_VARIABLE;
            }
            da_add(sizeof(char),buf,&str[i]);
            i++;
            break;
        case '~':
            if((!i||(ret&PARSE_IS_VARIABLE&&str[i-1]=='=')||str[i-1]==' ')&&(!str[i+1]||str[i+1]=='/'||str[i+1]==' ')){
                ret|=IS_PARSED;
                parse_home(buf,str,&i);
                break;
            }
            da_add(sizeof(char),buf,&str[i]);
            i++;
            break;
        case '\\':
            i++;
            ret|=IS_PARSED;
            da_add(sizeof(char),buf,&str[i]);
            i++;
            break;
        default:
            da_add(sizeof(char),buf,&str[i]);
            i++;
            break;
        }
    }
    *inter=i;

    return ret;
}

int parse_command(command_t *now,const char *str,size_t *inter,int flg){
    int is_parsing_item=0,is_first_item=1;
    static int last_is_pipe=0;
    static command_redir_t redirector;

    while(str[*inter]!='\0'){
        da_str buf;
        da_init(&buf);
        
        int r=parse_item(now,&buf,str,inter);
        if(!buf.size){
            break;
        }
        if(r&PARSE_IS_ARRAY){
            continue;
        }

        da_add(sizeof(char),&buf,"");

        if(is_first_item&&!is_script){
            const char *als=get_alias(buf.arr);
            if(als&&!flg){
                is_first_item=0;
                free(buf.arr);
                size_t i=0;
                parse_command(now,als,&i,1);
                now->argvn--;
                is_parsing_item=1;
                continue;
            }
        }

        int tp=0;

        if(redirector.type){
            redirector._2=buf.arr;
            cm_add_cmd(now,&redirector,sizeof(command_redir_t));
            buf.arr=NULL;
            redirector.type=0;
        }else if(!(r&IS_PARSED)&&buf.size==2&&buf.arr[0]=='|'){
            tp=CMD_PIPE;
            cm_add_cmd(now,&tp,sizeof(int));
            free(buf.arr);
            last_is_pipe=1;
            cm_add_item(now,NULL);
            return 0;
        }else if(!(r&IS_PARSED)&&buf.size==2&&buf.arr[0]==';'){
            free(buf.arr);
            cm_add_item(now,NULL);
            return 0;
        }else if(!(r&IS_PARSED)&&buf.size==2&&buf.arr[0]=='&'){
            tp=CMD_BG_END;
            cm_add_cmd(now,&tp,sizeof(int));
            free(buf.arr);
            cm_add_item(now,NULL);
            return PARSE_IS_BACKGROUND;
        }else if(!(r&IS_PARSED)&&buf.size==3&&buf.arr[0]=='&'&&buf.arr[1]=='&'){
            tp=CMD_AND;
            cm_add_cmd(now,&tp,sizeof(int));
            free(buf.arr);
            cm_add_item(now,NULL);
            return 0;
        }else if(!(r&IS_PARSED)&&buf.size==3&&buf.arr[0]=='|'&&buf.arr[1]=='&'){
            command_redir_t tmp={.type=CMD_REDIR,.op=REDIR_DUP,._1=1,._2=malloc(sizeof("2"))};
            memcpy(tmp._2,"2",sizeof("2"));
            cm_add_cmd(now,&tmp,sizeof(command_redir_t));
            tp=CMD_PIPE;
            cm_add_cmd(now,&tp,sizeof(char));
            free(buf.arr);
            last_is_pipe=1;
            cm_add_item(now,NULL);
            return 0;
        }else if(!(r&IS_PARSED)&&buf.size==3&&buf.arr[0]=='|'&&buf.arr[1]=='|'){
            tp=CMD_OR;
            cm_add_cmd(now,&tp,sizeof(int));
            free(buf.arr);
            cm_add_item(now,NULL);
            return 0;
        }else if(!(r&IS_PARSED)&&(redirector.op=is_redirector(buf.arr,NULL,&redirector._1))){
            redirector.type=CMD_REDIR;
            if(redirector.op==REDIR_CLOSE){
                redirector.type=0;
            }
            free(buf.arr);
        }else if(r&PARSE_IS_VARIABLE&&!is_parsing_item&&is_variable(buf.arr)){
            command_var_t tmp={.type=CMD_VAR,.umask=VAR_EXIST,.var=buf.arr};
            cm_add_cmd(now,&tmp,sizeof(command_var_t));
            buf.arr=NULL;
        }else{
            is_parsing_item=1;
            is_first_item=0;
            cm_add_item(now,buf.arr);
        }

        if(r&PARSE_SUBPRASE_END){
            cm_add_item(now,NULL);
            return r;
        }
    }
    if(last_is_pipe){
        int tp=CMD_PIPE_END;
        cm_add_cmd(now,&tp,sizeof(int));
        last_is_pipe=0;
    }
    cm_add_item(now,NULL);
    return 0;
}

int parse_buffer(da_command *cmds,const char *str,size_t *inter){
    command_t cmd;
    if(cmds->size){
        cmd=cmds->arr[cmds->size-1];
        da_pop(sizeof(command_t),&cmds);
    }else{
        cm_init(&cmd);
    }
    
    size_t i=*inter;
    while(str[i]==' '){
        i++;
    }
    if(str[i]=='\0'){
        return 0;
    }

    int ret=0;
    size_t last_bg_loc=0;
    size_t j=0;
    while(str[i]!='\0'){
        ret=parse_command(&cmd,str,&i,0);
        da_add(sizeof(command_t),cmds,&cmd);
        if(ret&PARSE_PARENTHESES_END){
            break;
        }
        if(ret&PARSE_IS_BACKGROUND){
            int tp=CMD_BG;
            cm_add_cmd(&cmds->arr[last_bg_loc],&tp,sizeof(tp));
            last_bg_loc=j+1;
        }
        cm_init(&cmd);
        j++;
        if(ret&PARSE_SUBPRASE_END){
            break;
        }
    }


    now_name=restore_cmd(cmds->arr,cmds->size);
    for(size_t i=0;i<cmds->size;i++){
        command_t *p=&cmds->arr[i];
        if(p->argvn){
            execute_command(cmds,i);
        }
        for(size_t j=0;j<p->argvn;j++){
            free(p->argv[j]);
        }
        cm_clear(&cmds->arr[i]);
    }
    
    free(now_name);
    if(is_child){
        exit(g_ret);
    }

    if(ret&PARSE_SUBPRASE_END){
        *inter=i;
        return 2;
    }

    return 0;
}

int execute_command(da_command *cmds,size_t i){
    static int in_bg,skip;
    if(i==0){
        in_bg=0;
        skip=0;
    }
    command_t *p=&cmds->arr[i];
    if(in_bg&&!is_child){
        if(exe_bg_end(p)){
            in_bg=0;
        }
        return 0;
    }
    if(!now_name){
        now_name=restore_cmd(cmds->arr+i,cmds->size-i);
    }
    if(skip){
        skip=0;
        return 0;
    }

    if(exe_bg_start(p)){
        int pipes[2];
        char tmp[]="1";
        pipe(pipes);
        int pid=fork();
        if(pid){
            is_child=0;
            add_job(now_name,pid,JOB_RUNNING);
            if(jobmsgsiz<JOB_MSG_SIZE){
                jobmsg[jobmsgsiz++]=(jobmsg_t){.pid=pid,.stat=JOB_RUNNING};
            }
            now_name=NULL;
            in_bg=!exe_bg_end(p);
            setpgid(pid,pid);
            read(pipes[0],tmp,1);
            close(pipes[0]);
            close(pipes[1]);
            return 0;
        }else{
            setpgid(0,0);
            is_child=1;
            child_clear();
            write(pipes[1],tmp,1);
            close(pipes[0]);
            close(pipes[1]);
        }
    }
    int ret=0;
    ret=execute_command_parent(p);
    g_ret=ret&0x7f;
    int r1=ret>>8;
    now_pid=0;
    if(r1&EXE_AND&&g_ret){
        skip=1;
    }
    if(r1&EXE_OR&&!g_ret){
        skip=1;
    }
    if(r1&EXE_ADD_BG){
        in_bg=1;
    }
    if(r1&EXE_DEL_BG){
        if(is_child){
            exit(g_ret);
        }else{
            in_bg=0;
        }
    }
    return 0;
}


int exe_bg_start(command_t *cmd){
    for(size_t i=0;i<cmd->cmdsn;i++){
        if(*cmd->cmds[i]==CMD_BG){
            return 1;
        }
    }
    return 0;
}
int exe_bg_end(command_t *cmd){
    for(size_t i=0;i<cmd->cmdsn;i++){
        if(*cmd->cmds[i]==CMD_BG_END){
            return 1;
        }
    }
    return 0;
}

int exe_parse_redirector(int st,command_redir_t *redir){
    static darray_t(int) pips;
    static int now;
    if(st==EXE_CLEAR){
        now=0;
        for(size_t i=0;i<pips.size;i++){
            close(pips.arr[i]);
        }
        da_fake_clear(&pips);
        return 0;
    }

    int fd=-1;

    if(st&EXE_PARENT){
        int pipes[2]={-1,-1};
        if(redir->op==REDIR_HERE_STRING){
            if(!redir->_2){
                return EXE_WRONG_REDIR;
            }
            if(pipe(pipes)!=0){
                return EXE_WRONG_PIPE;
            }
            write(pipes[1],redir->_2,strlen(redir->_2));
            write(pipes[1],"\n",1);
            close(pipes[1]);

            da_add(sizeof(int),&pips,&pipes[0]);
        }else if(redir->op==REDIR_HERE_DOCUMENT){
            if(!redir->_2){
                return EXE_WRONG_REDIR;
            }
            if(pipe(pipes)!=0){
                return EXE_WRONG_PIPE;
            }
            size_t len=strlen(redir->_2);
            char *tmp=NULL;
            size_t tmp_size=0;
            while(1){
                da_fake_clear(&buffer);
                const char *ps=gets_var("PS2");
                input(ps);
                if(buffer.size==len&&!memcmp(buffer.arr,redir->_2,len)){
                    break;
                }
                tmp=realloc(tmp,sizeof(char)*(tmp_size+buffer.size+1));
                memcpy(tmp+tmp_size,buffer.arr,buffer.size);
                tmp_size+=buffer.size;
                tmp[tmp_size++]='\n';
            }
            write(pipes[1],tmp,tmp_size);
            close(pipes[1]);
            free(tmp);
            
            da_add(sizeof(int),&pips,&pipes[0]);
        }
        return 0;
    }
    num_t r;
    switch(redir->op){
    case REDIR_IN:
        if(!redir->_2){
            return EXE_WRONG_REDIR;
        }
        fd=open(redir->_2,O_RDONLY);
        if(fd<0){
            return EXE_WRONG_FILE;
        }
        if(redir->_1<0){
            redir->_1=STDIN_FILENO;
        }
        dup2(fd,redir->_1);
        break;
    case REDIR_OUT:
        if(!redir->_2){
            return EXE_WRONG_REDIR;
        }
        fd=open(redir->_2,O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(fd<0){
            return EXE_WRONG_FILE;
        }
        if(redir->_1<0){
            redir->_1=STDOUT_FILENO;
        }
        dup2(fd,redir->_1);
        break;
    case REDIR_OUT_ADD:
        if(!redir->_2){
            return EXE_WRONG_REDIR;
        }
        fd=open(redir->_2,O_APPEND);
        if(fd<0){
            return EXE_WRONG_FILE;
        }
        if(redir->_1<0){
            redir->_1=STDOUT_FILENO;
        }
        dup2(fd,redir->_1);
        break;
    case REDIR_DUP:
        if(!redir->_2){
            return EXE_WRONG_REDIR;
        }
        r=cmd_str_to_num(redir->_2);
        if(r.is_negative||r.unexcepted_char){
            return EXE_WRONG_FILE;
        }
        fd=r.num;
        dup2(fd,redir->_1);
    case REDIR_HERE_DOCUMENT:
    case REDIR_HERE_STRING:
        if(now>=pips.size){
            break;
        }
        if(redir->_1<0){
            redir->_1=STDIN_FILENO;
        }
        dup2(pips.arr[now],redir->_1);
        close(pips.arr[now]);
        now++;
        break;
    case REDIR_CLOSE:
        if(redir->_1<0){
            break;
        }
        close(redir->_1);
        break;
    }
    return 0;
}

int exe_parse_pipe(int cmd,int st,int *pipes,int *prev_pipe){
    if(st&EXE_PARENT){
        if(st&EXE_START){
            if(cmd==CMD_PIPE){
                pipe(pipes);
                return EXE_PIPE;
            }
            return 0;
        }

        if(cmd==CMD_PIPE_END){
            close(*prev_pipe);
            *prev_pipe=-1;
            return 0;
        }
        close(pipes[1]);
        if(*prev_pipe>0){
            close(*prev_pipe);
        }
        *prev_pipe=pipes[0];
        return 0;
    }

    if(*prev_pipe>0){
        dup2(*prev_pipe,STDIN_FILENO);
        close(*prev_pipe);
    }

    if(cmd==CMD_PIPE){
        close(pipes[0]);
        dup2(pipes[1],STDOUT_FILENO);
        close(pipes[1]);
    }
    return 0;
}

int exe_parse_cmd(command_t *cmd,int st){
    static int pipes[2];
    static int prev_pipe=-1;

    if(!(st&EXE_START)){
        recovery_tmp_env();
        exe_parse_redirector(EXE_CLEAR,NULL);
    }

    darray_t(char*) tmp;
    da_init(&tmp);

    int r=0;

    for(size_t i=0;i<cmd->cmdsn;i++){
        if(*cmd->cmds[i]==CMD_PIPE||*cmd->cmds[i]==CMD_PIPE_END){
            r|=exe_parse_pipe(*cmd->cmds[i],st,pipes,&prev_pipe);
        }else if(st&EXE_START&&*cmd->cmds[i]==CMD_REDIR){
            r|=exe_parse_redirector(st,(void*)cmd->cmds[i]);
        }else if(st&EXE_START&&*cmd->cmds[i]==CMD_VAR){
            r|=set_tmp_env(((command_var_t*)cmd->cmds[i])->var);
        }else if(!(st&EXE_START)&&*cmd->cmds[i]==CMD_BG_END){
            r|=EXE_DEL_BG;
        }else if(*cmd->cmds[i]==CMD_AND){
            r|=EXE_AND;
        }else if(*cmd->cmds[i]==CMD_OR){
            r|=EXE_OR;
        }
    }
    return r;
}

int execute_command_child(command_t *cmd){
    cmd_execvpe(cmd->argv[0],cmd->argv,env.arr);

    cmd->argv=realloc(cmd->argv,(cmd->argvn+1)*sizeof(char*));
    memmove(cmd->argv+1,cmd->argv,cmd->argvn*sizeof(char*));
    cmd->argvn++;
    cmd->argv[0]=g_argv[0];
    cmd_execvpe(cmd->argv[0],cmd->argv,env.arr);
    write(STDERR_FILENO,cmd->argv[0],strlen(cmd->argv[0]));
    write(STDERR_FILENO,": command not found\n",20);
    return 127;
}

int execute_command_parent(command_t *cmd){
    if(!cmd->argv[0]){
        for(size_t i=0;i<cmd->cmdsn;i++){
            if(*cmd->cmds[i]!=CMD_VAR){
                continue;
            }
            command_var_t *r=(void*)cmd->cmds[i];
            set_var(r->var,r->umask);
        }
        return 0;
    }
    command_func f=get_builtin_cmd(cmd->argv[0]);
    int r1=exe_parse_cmd(cmd,EXE_START|EXE_PARENT);
    int r2=0;

    if(f&&!(r1&EXE_PIPE)&&f!=sh_echo){
        now_pid=-1;

        r2=f(cmd->argv);
        exe_parse_cmd(cmd,EXE_PARENT);
        return r2|(r1<<8);
    }

    int pipes[2]={};
    pipe(pipes);

    int pid=fork();
    if(pid<0){
        close(pipes[0]);
        close(pipes[1]);
        write(STDERR_FILENO,"failed to fork\n",15);
        return 127;
    }
    if(pid==0){
        char c;
        read(pipes[0],&c,1);
        close(pipes[0]);
        close(pipes[1]);

        setpgid(0,0);
        exe_parse_cmd(cmd,EXE_START);
        set_signal_handler(1);
        if(f){
            r2=f(cmd->argv);
        }else{
            r2=execute_command_child(cmd);
        }
        _exit(r2);
    }

    close(pipes[0]);

    setpgid(pid,pid);

    struct sigaction sa;
    sa.sa_handler=SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags=SA_RESTART;
    sigaction(SIGTTOU,&sa,NULL);
    tcsetpgrp(STDIN_FILENO,pid);

    now_pid=pid;

    write(pipes[1],"1\n",2);
    close(pipes[1]);

    int status=0;
    while(1){
        if(waitpid(pid,&status,WUNTRACED)<0){
            continue;
        }

        if(WIFEXITED(status)||WIFSIGNALED(status)){
            break;
        }
        if(WIFSTOPPED(status)){
            if(is_child){
                raise(SIGSTOP);
                continue;
            }
            int pipes[2];
            char tmp[]="1";
            pipe(pipes);
            int pid=fork();
            if(!pid){
                setpgid(0,0);
                is_child=1;
                child_clear();
                write(pipes[1],tmp,1);
                close(pipes[0]);
                close(pipes[1]);
                continue;
            }
            is_child=0;
            setpgid(pid,pid);
            add_job(now_name,pid,JOB_STOPPED);
            if(jobmsgsiz<JOB_MSG_SIZE){
                jobmsg[jobmsgsiz++]=(jobmsg_t){.pid=pid,.stat=JOB_RUNNING};
            }
            read(pipes[0],tmp,1);
            close(pipes[0]);
            close(pipes[1]);
            kill(pid,SIGSTOP);
            now_pid=0;
        
            exe_parse_cmd(cmd,EXE_PARENT);
            r1|=EXE_ADD_BG;
            now_name=NULL;

            tcsetpgrp(STDIN_FILENO,getpgrp());
            sa.sa_handler=SIG_DFL;
            sigaction(SIGTTOU,&sa,NULL);
            return g_ret|(r1<<8);
        }
    }

    tcsetpgrp(STDIN_FILENO,getpgrp());
    sa.sa_handler=SIG_DFL;
    sigaction(SIGTTOU,&sa,NULL);

    int r3=exe_parse_cmd(cmd,EXE_PARENT);
    if(r3&EXE_DEL_BG){
        exit(WIFEXITED(status)?127:WEXITSTATUS(status));
    }
    if(!WIFEXITED(status)){
        return WTERMSIG(status);
    }
    return WEXITSTATUS(status)|(r1<<8);
}


