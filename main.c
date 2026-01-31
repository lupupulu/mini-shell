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


int cm_init(command_t *cm);
int cm_add_item(command_t *cm,char *item);
int cm_add_cmd(command_t *cm,char *cmd);
int cm_clear(command_t *cm);

int insert_to_buffer(const char *str,size_t len,size_t pos);
int delete_to_buffer(size_t len,size_t pos);
char parse_variable(da_str *buf,const char *str,size_t *inter);
char parse_single_quote(da_str *buf,const char *str,size_t *inter);
char parse_quote(da_str *buf,const char *str,size_t *inter);
int parse_item(da_str *buf,const char *str,size_t *inter);
int parse_command(command_t *now,size_t *inter);
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

char parse_variable(da_str *buf,const char *str,size_t *inter){
    size_t i=*inter;
    if(str[i+1]=='@'&&(str[i+2]==' '||str[i+2]=='\0')){
        if(quote_flg){
            buffer.arr[i+1]='*';
            return parse_variable(buf,str,inter);
        }
        delete_to_buffer(2,i+1);
        size_t cnt=0;
        for(int j=1;j<=g_argc;j++){
            size_t len=strlen(g_argv[j]);
            insert_to_buffer("\'",1,cnt+i);
            cnt+=1;
            insert_to_buffer(g_argv[j],len,cnt+i);
            cnt+=len;
            insert_to_buffer("\'",1,cnt+i);
            cnt+=1;
            if(j!=g_argc){
                insert_to_buffer(" ",1,cnt+i);
                cnt+=1;
            }
        }
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
    i++;
    while(str[i]!='\0'&&str[i]!='\''){
        da_add(sizeof(char),buf,&str[i]);
        i++;
    }
    if(str[i]=='\0'){
        return '\'';
    }
    i++;
    *inter=i;
    return 0;
}

char parse_quote(da_str *buf,const char *str,size_t *inter){
    size_t i=*inter;
    i++;
    quote_flg=1;
    while(str[i]!='\0'&&str[i]!='\"'){
        switch(str[i]){
        case '$':
            *inter=i;
            parse_variable(buf,str,inter);
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

int parse_item(da_str *buf,const char *str,size_t *inter){
    size_t i=*inter;
    while(str[i]==' '){
        i++;
    }

    int ret=0;

    while(str[i]!='\0'&&str[i]!=' '){
        char r=0;
        switch(str[i]){
        case '$':
            ret|=0b1;
            r=parse_variable(buf,str,&i);
            break;
        case '\'':
            ret|=0b1;
            r=parse_single_quote(buf,str,&i);
            break;
        case '\"':
            ret|=0b1;
            r=parse_quote(buf,str,&i);
            break;
        case '=':
            if(!(ret&0b1)){
                ret|=0b10;
            }
            da_add(sizeof(char),buf,&str[i]);
            i++;
            break;
        case '\\':
            ret|=0b1;
            i++;
        default:
            da_add(sizeof(char),buf,&str[i]);
            i++;
            break;
        }
        if(r){
            ret|=0b100;
            ret|=r<<8;
            break;
        }
    }
    *inter=i;

    return ret;
}

int parse_command(command_t *now,size_t *inter){
    int is_parse=0;
    int is_parsing_item=0;
    while(buffer.arr[*inter]!='\0'){
        da_str buf;
        da_init(&buf);
        int r=parse_item(&buf,buffer.arr,inter);
        da_add(sizeof(char),&buf,"");

        if(r&0b10&&!is_parsing_item){
            cm_add_cmd(now,buf.arr);
            free(buf.arr);
        }else{
            is_parsing_item=1;
            cm_add_item(now,buf.arr);
        }
        if(r&0b100){
            return 1;
        }
    }
    cm_add_item(now,NULL);
    return 0;
}

int parse_buffer(void){
    static da_str tmp_buffer;
    static da_command commands;

    size_t i=0;
    while(buffer.arr[i]==' '){
        i++;
    }
    if(i>=buffer.size){
        return 0;
    }

    da_add(sizeof(char),&buffer,"");
    char *p=malloc(sizeof(char)*(buffer.size));
    memcpy(p,buffer.arr,sizeof(char)*buffer.size);
    da_add(sizeof(char*),&history,&p);
    buffer.size--;

    int ret=0;
    while(i<buffer.size){
        command_t cmd;
        cm_init(&cmd);
        ret=parse_command(&cmd,&i);
        if(ret){
            return 1;
        }
        da_add(sizeof(command_t),&commands,&cmd);
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



int execute_command_child(command_t *cmd){
    set_terminal_echo(1);
    cmd_execvpe(cmd->argv[0],cmd->argv,env.arr);

    write(STDERR_FILENO,cmd->argv[0],strlen(cmd->argv[0]));
    write(STDERR_FILENO,": command not found\n",20);
    return 127;
}

int execute_command_parent(command_t *cmd){
    command_func f=is_builtin_cmd(cmd->argv);
    if(f){
        return f(cmd->argv);
    }
    int pid=fork();

    if(pid==0){
        _exit(execute_command_child(cmd));
    }else if(pid>0){
        int status;
        wait(&status);
        set_terminal_echo(0);
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
