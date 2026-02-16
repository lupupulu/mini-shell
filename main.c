#include <unistd.h>
#include <memory.h>
#include <sys/wait.h>
#include "mnsh.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#define __USE_POSIX
#include <signal.h>


int execute_command_parent(command_t *cmd);
int execute_command_child(command_t *cmd);

#define EXE_START  0b0001
#define EXE_PARENT 0b0010
#define EXE_CLEAR  0b0100

#define EXE_PIPE       0b00000001
#define EXE_WRONG_FILE 0b00000010
#define EXE_WRONG_PIPE 0b00000100
#define EXE_AND        0b00001000
#define EXE_OR         0b00010000
#define EXE_ADD_BG     0b00100000
#define EXE_DEL_BG     0b01000000

int exe_parse_redirector(int st,int redir,int a,const char *file);
int exe_parse_pipe(const char *cmd,int st,int *pipes,int *prev_pipe);
int exe_parse_cmd(command_t *cmd,int st);

int exe_bg_start(command_t *cmd);
int exe_bg_end(command_t *cmd);


int cm_init(command_t *cm);
int cm_add_item(command_t *cm,char *item);
int cm_add_cmd(command_t *cm,char *cmd);
int cm_clear(command_t *cm);

int insert_to_buffer(const char *str,size_t len,size_t pos);
int delete_to_buffer(size_t len,size_t pos);

char parse_variable(command_t *now,da_str *buf,const char *str,size_t *inter);
char parse_single_quote(da_str *buf,const char *str,size_t *inter);
char parse_quote(command_t *now,da_str *buf,const char *str,size_t *inter);
char parse_home(da_str *buf,const char *str,size_t *inter);

#define IS_PARSED           0b0001
#define PARSE_IS_VARIABLE   0b0010
#define PARSE_IS_CONTINUE   0b0100
#define PARSE_IS_BACKGROUND 0b1000

int parse_item(command_t *now,da_str *buf,const char *str,size_t *inter);
int parse_command(command_t *now,const char *str,size_t *inter,int flg);
int parse_buffer(void);

int execute_command(da_command *p,size_t i);

int main(int argc,char *const *argv,char *const *envi){
    char *tmp=NULL;
    da_add(sizeof(char*),&env,&tmp);
    unsigned i=0;
    while(envi[i]){
        set_var(envi[i],VAR_EXPORT);
        i++;
    }

    const char *code=get_var("LANG");
    if(code){
        size_t i=0;
        while(code[i]!='.'){
            i++;
        }
        i++;

        if(!strcmp(code+i,"UTF-8")){
            codeset=UTF8;
        }
    }


    int input_mod=IN_ECHO|IN_HANDLE_CHAR;

    if(argc>=2){
        const char *p=file_is_exist(argv[1],F_OK|X_OK,0);
        if(!p){
            return 127;
        }
        is_script=1;
        input_mod=0;
        dup2(open(p,O_RDONLY),STDIN_FILENO);
        g_argc=argc-2;
        cmd_unsigned_to_str(g_argc_s,MAX_LL_SIZE-1,g_argc);
        g_argv=&argv[1];
    }else{
        g_argc=0;
        cmd_unsigned_to_str(g_argc_s,MAX_LL_SIZE-1,0);
        g_argv=argv;
    }
    if(!isatty(STDIN_FILENO)||!isatty(STDOUT_FILENO)||!isatty(STDERR_FILENO)){
        is_script=1;
    }
    set_terminal_echo(0);
    set_signal_handler(0);

    setpgid(0,0);

    g_pid=getpid();
    cmd_unsigned_to_str(g_pid_s,MAX_LL_SIZE-1,g_pid);

    if(!is_script){
        set_var("PS1=$ ",0);
        set_var("PS2=> ",0);
        set_var("PS4=+ ",0);
        tcsetpgrp(STDIN_FILENO,getpgrp());
    }
    const char *psn="PS1";



    while(1){
        for(size_t i=0;i<exitjobpidsiz;i++){
            del_job_pid(exitjobpid[i]);
            printf("%d\n",exitjobpid[i]);
        }
        exitjobpidsiz=0;


        const char *ps=NULL;
        if(!is_script){
            ps=get_var(psn);
            write(STDOUT_FILENO,ps,strlen(ps));
        }
        da_init(&buffer);
        int r=input(input_mod);


        switch(r){
        case 127:
            return 127;
        case -1:
            set_terminal_echo(1);
            set_signal_handler(1);
            printf("\nexit\n");
            return 0;
        case 2:
        case 0:
            int ret=parse_buffer();
            if(ret==1){
                psn="PS2";
            }else{
                psn="PS1";
            }
            if(r==2){
                set_terminal_echo(1);
                return 0;
            }
            break;
        }
    }

    set_terminal_echo(1);
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



int quote_flg=0;

char parse_variable(command_t *now,da_str *buf,const char *str,size_t *inter){
    size_t i=*inter;
    if(str[i+1]=='@'&&(str[i+2]==' '||str[i+2]=='\0')){
        return 0;
    }

    da_str tmp;
    da_init(&tmp);
    if(str[i+1]=='{'){
        i+=2;
        while(str[i]!='\0'&&str[i]!='}'){
            da_add(sizeof(char),&tmp,&str[i]);
            i++;
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

    const char *r=get_var(tmp.arr);
    da_clear(&tmp);
    if(!r){
        return 0;
    }
    size_t j=0;
    while(r[j]!='\0'){
        da_add(sizeof(char),buf,&r[j]);
        j++;
    }
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
            *inter=i;
            parse_variable(now,buf,str,inter);
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
    const char *home=get_var("HOME");
    size_t j=0;
    while(home[j]){
        da_add(sizeof(char),buf,&home[j]);
        j++;
    }
    *inter=i;
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

    while(str[i]!='\0'&&str[i]!=' '){
        char r=0;
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
            size_t j=0;
            int redir=is_redirector(str+*inter,&j,NULL);
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
            r=parse_variable(now,buf,str,&i);
            break;
        case '\'':
            ret|=IS_PARSED;
            r=parse_single_quote(buf,str,&i);
            break;
        case '\"':
            ret|=IS_PARSED;
            r=parse_quote(now,buf,str,&i);
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
                r=parse_home(buf,str,&i);
                break;
            }
            da_add(sizeof(char),buf,&str[i]);
            i++;
            break;
        case '\\':
            i++;
            if(str[i]=='\0'){
                r='\n';
                break;
            }
            ret|=IS_PARSED;
            da_add(sizeof(char),buf,&str[i]);
            i++;
            break;
        default:
            da_add(sizeof(char),buf,&str[i]);
            i++;
            break;
        }
        if(r){
            ret|=PARSE_IS_CONTINUE;
            ret|=r<<8;
            break;
        }
    }
    *inter=i;

    return ret;
}

int parse_command(command_t *now,const char *str,size_t *inter,int flg){
    static da_str tmp_item;

    int is_parsing_item=0,is_first_item=1;
    int last_is_redirector=0;
    static int last_is_pipe=0;

    while(str[*inter]!='\0'){
        da_str buf;
        if(tmp_item.size){
            memcpy(&buf,&tmp_item,sizeof(da_str));
            memset(&tmp_item,0,sizeof(da_str));
        }else{
            da_init(&buf);
        }
        
        int r=parse_item(now,&buf,str,inter);
        if(r&PARSE_IS_CONTINUE){
            if(quote_flg){
                da_add(sizeof(char),&buf,"\n");
            }
            memcpy(&tmp_item,&buf,sizeof(da_str));
            return r;
        }
        if(!buf.size){
            break;
        }

        da_add(sizeof(char),&buf,"");

        if(is_first_item&&!is_script){
            const char *als=get_alias(buf.arr);
            if(als&&!flg){
                free(buf.arr);
                size_t i=0;
                parse_command(now,als,&i,1);
                now->argvn--;
                is_parsing_item=1;
            }
            is_first_item=0;
        }

        if(last_is_redirector){
            cm_add_cmd(now,buf.arr);
            free(buf.arr);
            last_is_redirector=0;
        }else if(!(r&IS_PARSED)&&buf.size==2&&buf.arr[0]=='|'){
            cm_add_cmd(now,"| ");
            free(buf.arr);
            last_is_pipe=1;
            cm_add_item(now,NULL);
            return 0;
        }else if(!(r&IS_PARSED)&&buf.size==2&&buf.arr[0]==';'){
            free(buf.arr);
            cm_add_item(now,NULL);
            return 0;
        }else if(!(r&IS_PARSED)&&buf.size==2&&buf.arr[0]=='&'){
            cm_add_cmd(now,"&e");
            free(buf.arr);
            cm_add_item(now,NULL);
            return PARSE_IS_BACKGROUND;
        }else if(!(r&IS_PARSED)&&buf.size==3&&buf.arr[0]=='&'&&buf.arr[1]=='&'){
            cm_add_cmd(now,"&&");
            free(buf.arr);
            cm_add_item(now,NULL);
            return 0;
        }else if(!(r&IS_PARSED)&&buf.size==3&&buf.arr[0]=='|'&&buf.arr[1]=='&'){
            cm_add_cmd(now,"1>&");
            cm_add_cmd(now,"2");
            cm_add_cmd(now,"| ");
            free(buf.arr);
            last_is_pipe=1;
            cm_add_item(now,NULL);
            return 0;
        }else if(!(r&IS_PARSED)&&buf.size==3&&buf.arr[0]=='|'&&buf.arr[1]=='|'){
            cm_add_cmd(now,"||");
            free(buf.arr);
            cm_add_item(now,NULL);
            return 0;
        }else if(!(r&IS_PARSED)&&is_redirector(buf.arr,NULL,NULL)){
            cm_add_cmd(now,buf.arr);
            free(buf.arr);
            last_is_redirector=1;
        }else if(r&PARSE_IS_VARIABLE&&!is_parsing_item&&is_variable(buf.arr)){
            cm_add_cmd(now,buf.arr);
            free(buf.arr);
        }else{
            is_parsing_item=1;
            cm_add_item(now,buf.arr);
        }
    }
    if(last_is_pipe){
        cm_add_cmd(now,"|e");
        last_is_pipe=0;
    }
    cm_add_item(now,NULL);
    return 0;
}

int parse_buffer(void){
    static da_command commands;
    static command_t cmd;

    static char *tmp_buffer;
    static size_t tmp_buffer_size;

    if(!buffer.arr){
        return 0;
    }

    size_t i=0;
    while(buffer.arr[i]==' '){
        i++;
    }
    if(i>=buffer.size){
        return 0;
    }

    if(tmp_buffer_size){
        da_pop(sizeof(char*),&history);
    }

    da_add(sizeof(char),&buffer,"");
    tmp_buffer=realloc(tmp_buffer,sizeof(char)*(tmp_buffer_size+buffer.size));
    memcpy(tmp_buffer+tmp_buffer_size,buffer.arr,sizeof(char)*buffer.size);
    buffer.size--;
    tmp_buffer_size+=buffer.size;

    int ret=0;
    size_t last_bg_loc=0;
    size_t j=0;
    while(i<buffer.size){
        ret=parse_command(&cmd,buffer.arr,&i,0);
        if(ret&PARSE_IS_CONTINUE){
            if(quote_flg){
                tmp_buffer=realloc(tmp_buffer,sizeof(char)*(tmp_buffer_size+2));
                tmp_buffer[tmp_buffer_size++]='\n';
                tmp_buffer[tmp_buffer_size]='\0';
            }else{
                tmp_buffer[--tmp_buffer_size]='\0';
            }
            da_add(sizeof(char*),&history,&tmp_buffer);
            return 1;
        }
        da_add(sizeof(command_t),&commands,&cmd);
        if(ret&PARSE_IS_BACKGROUND){
            cm_add_cmd(&commands.arr[last_bg_loc],"& ");
            last_bg_loc=j+1;
        }
        cm_init(&cmd);
        j++;
    }

    da_add(sizeof(char*),&history,&tmp_buffer);

    da_clear(&buffer);

    if(tmp_buffer_size){
        tmp_buffer=NULL;
        tmp_buffer_size=0;
    }

    set_terminal_echo(1);
    now_name=restore_cmd(commands.arr,commands.size);
    for(size_t i=0;i<commands.size;i++){
        command_t *p=&commands.arr[i];
        if(p->argvn){
            execute_command(&commands,i);
        }
        for(size_t j=0;j<p->argvn;j++){
            free(p->argv[j]);
        }
        cm_clear(&commands.arr[i]);
    }
    free(now_name);
    da_fake_clear(&commands);
    if(is_child){
        exit(g_ret);
    }
    set_terminal_echo(0);

    return 0;
}

int execute_command(da_command *cmds,size_t i){
    static int in_bg,skip;
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
        int pid=fork();
        if(pid){
            is_child=0;
            int i=add_job(now_name,pid,JOB_RUNNING);
            now_name=NULL;
            in_bg=!exe_bg_end(p);
            setpgid(pid,pid);
            // printf("%d\n",pid);
            int status;
            waitpid(pid,&status,WUNTRACED);
            kill(pid,SIGCONT);
            job.arr[i].stat=JOB_RUNNING;
            return 0;
        }else{
            setpgid(0,0);
            is_child=1;
            raise(SIGSTOP);
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
    if(cmd->cmdlen>2&&cmd->cmds[cmd->cmdlen-2]==' '&&cmd->cmds[cmd->cmdlen-3]=='&'){
        return 1;
    }
    return 0;
}
int exe_bg_end(command_t *cmd){
    size_t i=0;
    while(i<cmd->cmdlen&&cmd->cmds[i]){
        if(cmd->cmds[i]=='&'&&cmd->cmds[i+1]=='e'&&cmd->cmds[i+2]=='\0'){
            return 1;
        }
        i+=strlen(cmd->cmds+i)+1;
    }
    return 0;
}

int exe_parse_redirector(int st,int redir,int a,const char *file){
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
        if(redir==REDIR_HERE_STRING){
            if(pipe(pipes)!=0){
                return EXE_WRONG_PIPE;
            }
            write(pipes[1],file,strlen(file));
            write(pipes[1],"\n",1);
            close(pipes[1]);

            da_add(sizeof(int),&pips,&pipes[0]);
        }else if(redir==REDIR_HERE_DOCUMENT){
            if(pipe(pipes)!=0){
                return EXE_WRONG_PIPE;
            }
            size_t len=strlen(file);
            char *tmp=NULL;
            size_t tmp_size=0;
            while(1){
                da_fake_clear(&buffer);
                const char *ps=get_var("PS2");
                write(STDOUT_FILENO,ps,strlen(ps));
                input(IN_ECHO|IN_HANDLE_CHAR);
                if(buffer.size==len&&!memcmp(buffer.arr,file,len)){
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
    switch(redir){
    case REDIR_IN:
        fd=open(file,O_RDONLY);
        if(fd<0){
            return EXE_WRONG_FILE;
        }
        if(a<0){
            a=STDIN_FILENO;
        }
        dup2(fd,a);
        break;
    case REDIR_OUT:
        fd=open(file,O_WRONLY|O_CREAT);
        if(fd<0){
            return EXE_WRONG_FILE;
        }
        if(a<0){
            a=STDOUT_FILENO;
        }
        dup2(fd,a);
        break;
    case REDIR_OUT_ADD:
        fd=open(file,O_APPEND);
        if(fd<0){
            return EXE_WRONG_FILE;
        }
        if(a<0){
            a=STDOUT_FILENO;
        }
        dup2(fd,a);
        break;
    case REDIR_DUP:
        fd=cmd_str_to_unsigned(file,strlen(file));
        if(fd<0){
            return EXE_WRONG_FILE;
        }
        dup2(fd,a);
    case REDIR_HERE_DOCUMENT:
    case REDIR_HERE_STRING:
        if(a<0){
            a=STDIN_FILENO;
        }
        dup2(pips.arr[now],a);
        close(pips.arr[now]);
        now++;
        break;
    case REDIR_CLOSE:
        if(a<0){
            break;
        }
        close(a);
        break;
    }
    return 0;
}

int exe_parse_pipe(const char *cmd,int st,int *pipes,int *prev_pipe){
    if(st&EXE_PARENT){
        if(st&EXE_START){
            if(cmd[1]!='e'){
                pipe(pipes);
                return EXE_PIPE;
            }
            return 0;
        }

        if(cmd[1]=='e'){
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

    if(cmd[1]!='e'){
        close(pipes[0]);
        dup2(pipes[1],STDOUT_FILENO);
        close(pipes[1]);
    }
    return 0;
}

int exe_parse_cmd(command_t *cmd,int st){
    static int pipes[2];
    static int prev_pipe=-1;

    if(!st&EXE_START){
        recovery_tmp_env();
        exe_parse_redirector(EXE_CLEAR,0,-1,NULL);
    }

    size_t i=0;
    darray_t(char*) tmp;
    da_init(&tmp);

    int r=0;

    int redir=0,fd_a=-1;

    while(i<cmd->cmdlen&&cmd->cmds[i]){
        if(cmd->cmds[i]=='|'&&cmd->cmds[i+1]!='|'){
            r|=exe_parse_pipe(cmd->cmds+i,st,pipes,&prev_pipe);
        }else if(st&EXE_START&&(redir=is_redirector(cmd->cmds+i,NULL,&fd_a))){
            i+=strlen(cmd->cmds+i)+1;
            r|=exe_parse_redirector(st,redir,fd_a,cmd->cmds+i);
        }else if(st&EXE_START&&is_variable(cmd->cmds+i)){
            set_tmp_env(cmd->cmds+i);
        }else if(!(st&EXE_START)&&cmd->cmds[i]=='&'&&cmd->cmds[i+1]=='e'){
            r|=EXE_DEL_BG;
        }else if(cmd->cmds[i]=='|'&&cmd->cmds[i+1]=='|'){
            r|=EXE_OR;
        }else if(cmd->cmds[i]=='&'&&cmd->cmds[i+1]=='&'){
            r|=EXE_AND;
        }
        i+=strlen(cmd->cmds+i)+1;
    }
    return r;
}

int execute_command_child(command_t *cmd){
    set_terminal_echo(1);
    cmd_execvpe(cmd->argv[0],cmd->argv,env.arr);

    write(STDERR_FILENO,cmd->argv[0],strlen(cmd->argv[0]));
    write(STDERR_FILENO,": command not found\n",20);
    return 127;
}

int execute_command_parent(command_t *cmd){
    if(!cmd->argv[0]){
        size_t i=0;
        while(i<cmd->cmdlen&&cmd->cmds[i]){
            if(is_variable(cmd->cmds+i)){
                set_var(cmd->cmds+i,0);
            }
            i+=strlen(cmd->cmds+i);
        }
        return 0;
    }
    command_func f=is_builtin_cmd(cmd->argv);
    int r1=exe_parse_cmd(cmd,EXE_START|EXE_PARENT);
    int r2=0;

    if(f&&!(r1&EXE_PIPE)){
        now_pid=-1;

        r2=f(cmd->argv);
        exe_parse_cmd(cmd,EXE_PARENT);
        return r2|(r1<<8);
    }

    int pid=fork();
    if(pid==0){
        exe_parse_cmd(cmd,EXE_START);
        set_signal_handler(1);
        if(f){
            r2=f(cmd->argv);
        }else{
            r2=execute_command_child(cmd);
        }
        _exit(r2);
    }else if(pid>0){
        now_pid=pid;

        int status=0;
        waitpid(pid,&status,0);
        int r3=exe_parse_cmd(cmd,EXE_PARENT);
        if(r3&EXE_DEL_BG){
            exit(WIFEXITED(status)?127:WEXITSTATUS(status));
        }
        if(!WIFEXITED(status)){
            return 127;
        }
        return WEXITSTATUS(status)|(r1<<8);
    }
    write(STDERR_FILENO,"failed to fork\n",15);
    return 127;
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
int cm_add_cmd(command_t *cm,char *cmd){
    size_t len=strlen(cmd);
    if(!cm->cmds){
        cm->cmds=malloc((len+2)*sizeof(char));
        if(!cm->cmds){
            return 127;
        }
        memcpy(cm->cmds,cmd,len*sizeof(char));
        cm->cmds[len]='\0';
        cm->cmds[len]='\0';
        cm->cmdlen=len+1;
        return 0;
    }
    void *p=realloc(cm->cmds,(cm->cmdlen+len+2)*sizeof(char*));
    if(!p){
        return 127;
    }
    cm->cmds=p;
    memcpy(cm->cmds+cm->cmdlen,cmd,len+1);
    cm->cmdlen+=len+1;
    cm->cmds[cm->cmdlen]='\0';
    return 0;
}
int cm_clear(command_t *cm){
    free(cm->argv);
    free(cm->cmds);
    memset(cm,0,sizeof(command_t));
    return 0;
}
