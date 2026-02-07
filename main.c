#include <termios.h>
#include <unistd.h>
#include <memory.h>
#include <sys/wait.h>
#include "mnsh.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

int g_argc;
char g_argc_s[MAX_LL_SIZE];
char* const* g_argv;
int g_ret;
char g_ret_s[MAX_LL_SIZE];
int g_pid;
char g_pid_s[MAX_LL_SIZE];

codeset_t codeset;

int is_script;

int execute_command_parent(command_t *cmd);
int execute_command_child(command_t *cmd);

#define EXE_START  0b01
#define EXE_PARENT 0b10

#define EXE_PIPE   0b01

int exe_parse_pipe(const char *cmd,int st,int *pipes,int *prev_pipe);
int exe_parse_cmd(command_t *cmd,int st);



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

#define IS_PARSED         0b0001
#define PARSE_IS_VARIABLE 0b0010
#define PARSE_IS_CONTINUE 0b0100

int parse_item(command_t *now,da_str *buf,const char *str,size_t *inter);
int parse_command(command_t *now,const char *str,size_t *inter,int flg);
int parse_buffer(void);

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
    if(set_terminal_echo(0)){
        is_script=1;
    }

    g_pid=getpid();
    cmd_unsigned_to_str(g_pid_s,MAX_LL_SIZE-1,g_pid);

    set_var("PS1=$ ",0);
    set_var("PS2=> ",0);
    set_var("PS4=+ ",0);

    int con=0;

    const char *psn="PS1";

    while(1){
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
#ifdef BASIC_INPUT
            printf("\nexit\n");
#endif
            return 0;
        case 2:
        case 0:
            if(!con){
            }
            int ret=parse_buffer();
            con=0;
            if(ret==1){
                psn="PS2";
                con=1;
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

#ifdef BASIC_INPUT
    printf("exit\n");
#endif

    return 0;
}
#ifdef BASIC_INPUT
int set_terminal_echo(int enable){
    return 0;
}
#else
int set_terminal_echo(int enable){
    if(is_script){
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
        new_termios.c_cc[VMIN]=1;
        new_termios.c_cc[VTIME]=0;
    }

    if(tcsetattr(STDIN_FILENO,TCSANOW,&new_termios)==-1){
        return 1;
    }

    return 0;
}
#endif

int insert_to_buffer(const char *str,size_t len,size_t pos){
    da_resize(sizeof(char),&buffer,buffer.size+len);
    memmove(buffer.arr+pos+len,buffer.arr+pos,sizeof(char)*(buffer.size-pos));
    memcpy(buffer.arr+pos,str,len);
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

    int ret=0;

    while(str[i]!='\0'&&str[i]!=' '){
        char r=0;
        switch(str[i]){
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
            ret|=IS_PARSED;
            i++;
            if(str[i]=='\0'){
                r='\n';
                break;
            }
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
            return 1;
        }
        if(!buf.size){
            break;
        }

        da_add(sizeof(char),&buf,"");

        if(is_first_item){
            const char *als=get_alias(buf.arr);
            if(als&&!flg){
                free(buf.arr);
                size_t i=0;
                parse_command(now,als,&i,1);
                now->argvn--;
            }else{
                cm_add_item(now,buf.arr);
            }
            is_parsing_item=1;
            is_first_item=0;
        }else if(!r&IS_PARSED&&buf.size==2&&buf.arr[0]=='|'){
            cm_add_cmd(now,"| ");
            free(buf.arr);
            last_is_pipe=1;
            return 0;
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
    while(i<buffer.size){
        ret=parse_command(&cmd,buffer.arr,&i,0);
        if(ret){
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
        cm_init(&cmd);
    }

    da_add(sizeof(char*),&history,&tmp_buffer);

    if(tmp_buffer_size){
        tmp_buffer=NULL;
        tmp_buffer_size=0;
    }

    for(size_t i=0;i<commands.size;i++){
        command_t *p=&commands.arr[i];
        if(p->argvn){
            g_ret=execute_command_parent(p);
            for(size_t j=0;j<p->argvn;j++){
                free(p->argv[j]);
            }
        }
        cm_clear(&commands.arr[i]);
    }
    da_fake_clear(&commands);


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
    }

    size_t i=0;
    darray_t(char*) tmp;
    da_init(&tmp);

    int r=0;

    while(i<cmd->cmdlen&&cmd->cmds[i]){
        if(cmd->cmds[i]=='|'){
            r|=exe_parse_pipe(cmd->cmds+i,st,pipes,&prev_pipe);
        }else if(st&EXE_START&&is_variable(cmd->cmds+i)){
            set_tmp_env(cmd->cmds+i);
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
    int r=exe_parse_cmd(cmd,EXE_START|EXE_PARENT);

    if(f&&!r&EXE_PIPE){
        r=f(cmd->argv);
        exe_parse_cmd(cmd,EXE_PARENT);
        return r;
    }

    int pid=fork();
    if(pid==0){
        exe_parse_cmd(cmd,EXE_START);
        if(f){
            r=f(cmd->argv);
        }else{
            r=execute_command_child(cmd);
        }
        _exit(r);
    }else if(pid>0){
        int status;
        wait(&status);
        set_terminal_echo(0);
        exe_parse_cmd(cmd,EXE_PARENT);
        if(!WIFEXITED(status)){
            return 127;
        }
        return WEXITSTATUS(status);
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
