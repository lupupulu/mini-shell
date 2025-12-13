#include <termios.h>
#include <unistd.h>
#include <memory.h>
#include <sys/wait.h>
#include "mnsh.h"
#include <stdio.h>

static char *last_item[PARAM_BUF_ITEM];
static unsigned last_item_len;

char buffer[BUF_SIZE];
char start='$';

int set_terminal_echo(int enable);
int execute_extern_command(char * const *argv);
unsigned handle_backslash(char *r,const char *c);

int parse_buf_to_param(char *buf,unsigned len);



int main(int argc,const char **argv,char **env){

    if(set_terminal_echo(0)){
        return 1;
    }

    unsigned i=0;
    while(env[i]){
        set_var(env[i],VAR_EXPORT);
        i++;
    }

    unsigned rlen=0;
    int con=0;

    while(1){
        write(STDOUT_FILENO,&start,1);
        write(STDOUT_FILENO," ",1);
        buffer[0]='\0';
        int r=input(buffer,BUF_SIZE,&rlen,IN_ECHO|IN_HANDLE_CHAR);
        switch(r){
        case 127:
            return 127;
        case -1:
            return set_terminal_echo(1);
        case 0:
            start='$';
            if(!con){
                item_len=0;
            }
            r=parse_buf_to_param(buffer,rlen);
            con=0;
            if(r==1){
                start='>';
                con=1;
            }
            break;
        }
    }

    

    return 0;
}

int set_terminal_echo(int enable){
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

    if(enable){
        new_termios.c_lflag|=(ECHO|ICANON|ECHOE|ECHOK|ECHONL);
    }else{
        new_termios.c_lflag&=~(ECHO|ICANON|ECHOE|ECHOK|ECHONL);
        new_termios.c_cc[VMIN]=1;
        new_termios.c_cc[VTIME]=0;
    }

    if(tcsetattr(STDIN_FILENO,TCSANOW,&new_termios)==-1){
        return 1;
    }

    return 0;
}


int parse_buf_to_param(char *buf,unsigned len){
    static char param[PARAM_BUF_SIZE];

    static char *item[PARAM_BUF_ITEM];
    static unsigned item_len;

    static char temp[BUF_SIZE];
    static unsigned int temp_len;

    if(len&&buf[len-1]=='\\'&&temp_len+len<BUF_SIZE){
        if(temp_len){
            history_remove();
        }
        memcpy(temp+temp_len,buf,len);
        temp_len+=len;
        temp[temp_len]='\0';
        history_add(temp,temp_len);
        temp[--temp_len]='\0';
        return 1;
    }

    if(temp_len){
        history_remove();
        memcpy(temp+temp_len,buf,len);
        temp_len+=len;
        temp[temp_len]='\0';
        memcpy(buf,temp,temp_len+1);
        len=temp_len;
    }

    unsigned int param_len=(unsigned long long)(item[item_len]-item[0]);

    int skip_flg=1;
    char sign=0;
    int note=0;

    int var=0,sp_var=0,var_brackets=0;
    unsigned var_start=0;
    unsigned var_len,var_cnt;

    int ret=0;

    for(unsigned int i=0;i<len&&item_len<PARAM_BUF_ITEM-1&&param_len<PARAM_BUF_SIZE;i++){
        if(
            (var&&!var_brackets&&!(IS_LEGAL(buf[i])||sp_var)&&buf[i]!='{')||
            var_brackets==2
        ){
            var_len=0,var_cnt=0;
            param[param_len]='\0';
            get_var(param+var_start,PARAM_BUF_SIZE-var_start-1,param+var_start+1,&var_len,&var_cnt);
            param_len=var_start+var_cnt;
            var=0;
            var_brackets=0;
            sp_var=0;
        }
        if(buf[i]==' '&&!sign){
            if(!skip_flg){
                skip_flg=1;
                param[param_len++]='\0';
            }
            continue;
        }else if(!var_brackets&&buf[i]=='#'){
            if(!sign){
                break;
            }
            note=1;
        }else if(buf[i]=='\"'||buf[i]=='\''){
            if(!sign){
                sign=buf[i];
                continue;
            }
            if(sign&&sign==buf[i]){
                sign=0;
                note=0;
                continue;
            }
        }else if(sign!='\''&&buf[i]=='$'){
            var=1;
            var_start=param_len;
        }else if(var&&buf[i]=='{'){
            var_brackets=1;
        }else if(var_brackets==1&&buf[i]=='}'){
            var_brackets=2;
        }
        if(
            IS_SPECIAL_VARIABLE(buf[i])&&
            ((var&&!var_brackets&&var_start==i-1)||
            (var_brackets==1&&var_start==i-2))
        ){
            sp_var=1;
        }
        if(skip_flg){
            skip_flg=0;
            item[item_len]=&param[param_len];
            item_len++;
        }
        if(sign!='\''&&buf[i]=='~'&&(!i||buf[i-1]==' ')&&(buf[i+1]=='/'||buf[i+1]==' '||buf[i+1]=='\0')){
            var_len=0,var_cnt=0;
            get_var(param+param_len,PARAM_BUF_SIZE-param_len-1,"HOME",&var_len,&var_cnt);
            param_len+=var_cnt;
            continue;
        }
        if(buf[i]=='\\'&&i+1<len){
            ret=handle_backslash(&param[param_len++],&buf[i+1]);
            i+=ret;
            continue;
        }
        if(note){
            continue;
        }
        param[param_len++]=buf[i];
    }
    if(var){
        var_len=0,var_cnt=0;
        param[param_len]='\0';
        get_var(param+var_start,PARAM_BUF_SIZE-var_start,param+var_start+1,&var_len,&var_cnt);
        param_len=var_start+var_cnt;
        var=0;
    }

    param[param_len]='\0';

    // for(int i=0;i<*item_len;i++){
    //     printf("%d %s\n",i,item[i]);
    // }
    if(!item_len){
        return 0;
    }

    // printf("%s %d\n",buf,len);
    history_add(buf,len);

    item[item_len]=NULL;
    command_func f=is_builtin_cmd(item);
    if(f){
        f(item);
        item_len=0;
        item[0]=0;
        return 0;
    }

    execute_extern_command(item);

    item_len=0;
    item[0]=0;

    temp_len=0;

    return 0;
}

unsigned handle_backslash(char *r,const char *c){
    if(c[0]>='0'&&c[0]<='9'){
        *r=0;
        register unsigned i=0;
        while(i<3){
            if(c[i]<'0'||c[i]>'9'){
                break;
            }
            *r*=10;
            *r+=c[i]-'0';
            i++;
        }
        return i;
    }else if(c[0]=='x'){
        *r=0;
        register unsigned i=0;
        while(i<2){
            if(c[i]>='0'&&c[i]<='9'){
                *r*=16;
                *r+=c[i]-'0';
            }else if(c[i]>='A'&&c[i]<='F'){
                *r*=16;
                *r+=c[i]-'A';
            }else if(c[i]>='a'&&c[i]<='f'){
                *r*=16;
                *r+=c[i]-'a';
            }else{
                break;
            }
            i++;
        }
        return i;
    }
    switch(c[0]){
    case 't':
        *r='\t';
        return 1;
    case 'n':
        *r='\n';
        return 1;
    case 'r':
        *r='\r';
        return 1;
    default:
        *r=c[0];
        return 1;
    }
    return 0;
}

int execute_extern_command(char * const *argv){
    int pid=fork();
    if(!pid){
        cmd_execvpe(argv[0],argv,(char*const*)env_vec);
        write(STDERR_FILENO,argv[0],strlen(argv[0]));
        write(STDERR_FILENO,": command not found\n",20);
        _exit(127);
    }else if(pid>0){
        int status;
        wait(&status);
        return 0;
    }
    write(STDERR_FILENO,"failed to fork\n",15);
    return 1;
}

