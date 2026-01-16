#include <termios.h>
#include <unistd.h>
#include <memory.h>
#include <sys/wait.h>
#include "mnsh.h"
#include <stdio.h>
#include <stdlib.h>

darray_t(char*) items;
int g_argc;
char g_argc_s[MAX_LL_SIZE];
char* const* g_argv;
int g_ret;
char g_ret_s[MAX_LL_SIZE];
int g_pid;
char g_pid_s[MAX_LL_SIZE];

codeset_t codeset;

int is_script;

int execute_extern_command(char * const *argv);

void clear(void);

// unsigned handle_backslash(char *r,const char *c);
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
            clear();
            return 127;
        }
        is_script=1;
        input_mod=0;
        freopen(p,"r",stdin);
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
            clear();
            return 127;
        case -1:
            clear();
            set_terminal_echo(1);
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
                clear();
                set_terminal_echo(1);
                return 0;
            }
            break;
        }
    }

    clear();
    set_terminal_echo(1);

    return 0;
}

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


int parse_buffer(void){
    size_t i=0;
    da_str str;
    da_init(&str);
    da_str tmp;

    char quote=0;

    while(i==' '){
        i++;
    }
    while(i<buffer.size){
        if(buffer.arr[i]=='\\'){
            if(i==buffer.size-1){
                da_add(sizeof(char),&str,"");
                da_add(sizeof(char*),&items,&str.arr);
                return 1;
            }
            da_add(sizeof(char),&str,&buffer.arr[i+1]);
            i+=2;
            continue;
        }else if(!quote&&buffer.arr[i]==' '){
            da_add(sizeof(char),&str,"");
            da_add(sizeof(char*),&items,&str.arr);
            da_init(&str);
            while(i<buffer.size&&buffer.arr[i]==' '){
                i++;
            }
        }else if(buffer.arr[i]=='$'){
            if(buffer.arr[i+1]=='@'){
                for(int i=1;i<=g_argc;i++){
                    size_t len=strlen(g_argv[i])+1;
                    char *str=malloc(len*sizeof(char));
                    memcpy(str,g_argv[i],len*sizeof(char));
                    da_add(sizeof(char*),&items,&str);
                }
                i+=2;
            }
            da_init(&tmp);
            if(buffer.arr[i+1]=='{'){
                i+=2;
                while(i<buffer.size&&buffer.arr[i]!='}'){
                    da_add(sizeof(char),&tmp,&buffer.arr[i]);
                    i++;
                }
                i++;
                da_add(sizeof(char),&tmp,"");
            }else if(IS_SPECIAL_VARIABLE(buffer.arr[i+1])){
                i++;
                da_add(sizeof(char),&tmp,&buffer.arr[i]);
                da_add(sizeof(char),&tmp,"");
                i++;
            }else{
                i++;
                while(i<buffer.size&&IS_LEGAL(buffer.arr[i])){
                    da_add(sizeof(char),&tmp,&buffer.arr[i]);
                    i++;
                }
                da_add(sizeof(char),&tmp,"");
            }
            const char *r=get_var(tmp.arr);
            da_clear(&tmp);
            if(!r){
                continue;
            }
            size_t j=0;
            while(r[j]!='\0'){
                da_add(sizeof(char),&str,&r[j]);
                j++;
            }
        }else if(buffer.arr[i]=='\''){
            if(quote=='\''){
                quote=0;
            }else if(!quote){
                quote='\'';
            }
            i++;
            while(i<buffer.size&&buffer.arr[i]!='\''){
                da_add(sizeof(char),&str,&buffer.arr[i]);
                i++;
            }
            if(buffer.arr[i]=='\''){
                quote=0;
            }
            i++;
        }else if(buffer.arr[i]=='\"'){
            if(quote=='\"'){
                quote=0;
            }else if(!quote){
                quote='\"';
            }
            i++;
        }else{
            da_add(sizeof(char),&str,&buffer.arr[i]);
            i++;
        }
    }

    if(str.arr){
        da_add(sizeof(char),&str,"");
        da_add(sizeof(char*),&items,&str.arr);
    }
    
    if(!items.size){
        return 0;
    }

    char *null=NULL;
    da_add(sizeof(char*),&items,&null);

    char *p=malloc(sizeof(char)*(buffer.size+1));
    memcpy(p,buffer.arr,buffer.size);
    p[buffer.size]='\0';
    da_add(sizeof(char*),&history,&p);

    if(quote){
        return 1;
    }

    command_func f=is_builtin_cmd(items.arr);
    if(f){
        g_ret=f(items.arr);
    }else{
        set_terminal_echo(1);
        g_ret=execute_extern_command(items.arr);
        set_terminal_echo(0);
    }
    // else if(is_variable(items.arr[0])){
    //     g_ret=set_var(items.arr[0],0)?127:0;
    // }

    for(size_t i=0;i<items.size;i++){
        free(items.arr[i]);
    }
    da_fake_clear(&items);


    return 0;
}

int execute_extern_command(char * const *argv){
    int pid=fork();
    if(!pid){
        cmd_execvpe(argv[0],argv,env.arr);
        write(STDERR_FILENO,argv[0],strlen(argv[0]));
        write(STDERR_FILENO,": command not found\n",20);
        _exit(127);
    }else if(pid>0){
        int status;
        wait(&status);
        if(!WIFEXITED(status)){
            return 127;
        }
        return WEXITSTATUS(status);
    }
    write(STDERR_FILENO,"failed to fork\n",15);
    return 1;
}


void clear(void){
    for(size_t i=0;i<history.size;i++){
        free(history.arr[i]);
    }
    da_clear(&history);
    for(size_t i=0;i<variable.size;i++){
        free(variable.arr[i].var);
    }
    da_clear(&variable);
    da_clear(&env);
    for(size_t i=0;i<items.size;i++){
        free(items.arr[i]);
    }
    da_clear(&items);
}

