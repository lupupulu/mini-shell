#include <termios.h>
#include <unistd.h>
#include <memory.h>
#include <sys/wait.h>
#include "mnsh.h"
#include <stdio.h>

#define IS_LEGAL(c) (\
        ((c)>='A'&&(c)<='Z')||\
        ((c)>='a'&&(c)<='z')||\
        ((c)>='0'&&(c)<='9')||\
        (c)=='_'\
    )

static unsigned int item_len=0;

char buffer[BUF_SIZE];
static unsigned int len;
static unsigned int pos;
static int buffer_is_saved=0,now_is_bufffer=1;
static char start='$';

int set_terminal_echo(int enable);
int handle_escape_sequence(void);
int handle_invisible_char(char c);
unsigned handle_backslash(char *r,const char *c);
int execute_extern_command(char * const *argv);

int parse_buf_to_param(
    char *buf,
    unsigned len
);

static inline void insert_str(
    char *buf,
    unsigned pos,
    unsigned len,
    const char *c,
    unsigned c_len
);
static inline void insert(char *buf,unsigned pos,unsigned len,char c);
static inline void backspace(char *buf,unsigned pos,unsigned len);
static inline void delete(char *buf,unsigned pos,unsigned len);

int main(int argc,const char **argv,char **env){

    char c=0;
    int ret=0,con=0;

    if(set_terminal_echo(0)){
        return 1;
    }

    unsigned i=0;
    while(env[i]){
        set_var(env[i],VAR_EXPORT);
        i++;
    }

    while(1){
        write(STDOUT_FILENO,&start,1);
        write(STDOUT_FILENO," ",1);
        pos=0;
        len=0;
        buffer[0]='\0';
        while(1){
            ret=read(STDIN_FILENO,&c,1);
            // printf("%d\n",c);
            if(ret==-1){
                return 1;
            }
            if(c==0x04||ret==0){
                write(STDOUT_FILENO,"\nexit\n",6);
                set_terminal_echo(1);
                return 0;
            }
            if(c=='\n'){
                write(STDOUT_FILENO,"\n",1);
                start='$';
                if(!con){
                    item_len=0;
                }
                if(buffer_is_saved){
                    buffer_is_saved=0;
                    history_remove();
                }
                buffer_is_saved=0;
                now_is_bufffer=1;
                ret=parse_buf_to_param(
                    buffer,
                    len
                );
                // printf("%s\n",history_buffer);
                con=0;
                if(ret>0){
                    start='>';
                    con=1;
                }
                break;
            }else if(c==0x1b){
                ret=handle_escape_sequence();

                if(ret<0) return 1;
                else if(ret>0) continue;
                else c='[';

            }else if(c<0x20||c==127){
                handle_invisible_char(c);
                continue;
            }
            if(len<BUF_SIZE){
                insert(buffer,pos,len,c);
                pos++;
                len++;
            }else{
                write(STDOUT_FILENO,"\ncommand too long.\n",19);
                break;
            }
        }
    }

    set_terminal_echo(1);

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

int handle_escape_sequence(void){
    char c,tmp[8];
    int ret=read(0,&c,1);

    char space=0;

    if(ret!=1){
        return -1;
    }

    if(c=='['){
        ret=read(STDIN_FILENO,&c,1);
        if(ret!=1)return -1;
        switch(c){
        case 'A':
            // printf("%d %d\n",buffer_is_saved,now_is_bufffer);
            if(!buffer_is_saved){
                history_add(buffer,len);
                // printf("saved\n");
                buffer_is_saved=1;
                now_is_bufffer=0;
                // get_last_history(NULL);
            }
            if(get_last_history(buffer)){
                return 1;
            }
            write(STDOUT_FILENO,"\r\033[K",5);
            write(STDOUT_FILENO,&start,1);
            write(STDOUT_FILENO," ",1);

            len=strlen(buffer);
            pos=len;
            write(STDOUT_FILENO,buffer,len);
            return 1;
        case 'B':
            if((ret=get_next_history(buffer))!=0){
                if(!buffer_is_saved){
                    return 1;
                }
            }

            write(STDOUT_FILENO,"\r\033[K",5);
            write(STDOUT_FILENO,&start,1);
            write(STDOUT_FILENO," ",1);
            len=strlen(buffer);
            pos=len;
            write(STDOUT_FILENO,buffer,len);

            if(ret){
                history_remove();
                now_is_bufffer=1;
                buffer_is_saved=0;
            }

            return 1;
        case 'C':
            if(pos<len){
                write(STDOUT_FILENO,"\033[C",3);
                pos++;
            }
            return 1;
        case 'D':
            if(pos>0){
                write(STDOUT_FILENO,"\033[D",3);
                pos--;
            }
            return 1;
        case 'H':
            while(pos>0){
                write(STDOUT_FILENO,"\033[D",3);
                pos--;
            }
            return 1;
        case 'F':
            while(pos<len){
                write(STDOUT_FILENO,"\033[C",3);
                pos++;
            }
            return 1;
        case '3':
            ret=read(0,&c,1);
            if(ret==1&&c=='~'){
                delete(buffer,pos,len);
                if(pos<len){
                    len--;
                }
            }else{
                memcpy(tmp,"[[3",3);
                tmp[3]=c;
                insert_str(buffer,pos,len,tmp,4);
                len+=4;
                pos+=4;
                return 1;
            }
            return 1;
        case '1':
            ret=read(0,&c,1);
            if(ret!=1) return -1;
            memcpy(tmp,"[[1",3);
            tmp[3]=c;
            if(c!=';'){
                insert_str(buffer,pos,len,tmp,4);
                len+=4;
                pos+=4;
                return 1;
            }
            ret=read(0,&c,1);
            if(ret!=1) return -1;
            tmp[4]=c;
            if(c=='5'){
                ret=read(0,&c,1);
                tmp[5]=c;
                if(ret!=1) return -1;
                if(c=='D'){
                    if(pos&&buffer[pos-1]==' '){
                        space=1;
                    }
                    while(pos){
                        if(space&&buffer[pos-1]!=' '){
                            break;
                        }
                        if(!space&&buffer[pos-1]==' '){
                            break;
                        }
                        write(STDOUT_FILENO,"\033[D",3);
                        pos--;
                    }
                }else if(c=='C'){
                    if(pos<len&&buffer[pos+1]==' '){
                        space=1;
                    }
                    while(pos<len){
                        if(space&&buffer[pos+1]!=' '){
                            break;
                        }
                        if(!space&&buffer[pos+1]==' '){
                            break;
                        }
                        write(STDOUT_FILENO,"\033[C",3);
                        pos++;
                    }
                }else{
                    insert_str(buffer,pos,len,tmp,6);
                    len+=6;
                    pos+=6;
                    return 1;
                }
            }else{
                insert_str(buffer,pos,len,tmp,5);
                len+=5;
                pos+=5;
                return 1;
            }
            return 1;
        }
    }

    return 0;
}

int handle_invisible_char(char c){
    switch(c){
    case 0x01:
        while(pos>0){
            pos--;
            write(STDOUT_FILENO,"\b",1);
        }
        break;
    case 0x02:
        if(pos>0){
            pos--;
            write(STDOUT_FILENO,"\b",1);
        }
        break;
    case 0x06:
        if(pos<len){
            pos++;
            write(STDOUT_FILENO,&buffer[pos-1],1);
        }
        break;
    case 0x08:
    case 0x7f:
        backspace(buffer,pos,len);
        if(pos){
            len--;
            pos--;
        }
        break;
    case 0x17:
        int space=0;
        if(pos&&buffer[pos-1]==' '){
            space=1;
        }
        while(pos){
            if(space&&buffer[pos-1]!=' '){
                break;
            }
            if(!space&&buffer[pos-1]==' '){
                break;
            }
            backspace(buffer,pos,len);
            len--;
            pos--;
        }
        break;
    case 0x0b:
        while(pos<len){
            write(STDOUT_FILENO," ",1);
            pos++;
        }
        while(pos>0&&buffer[pos-1]!='\n'){
            pos--;
            write(STDOUT_FILENO,"\b \b",3);
        }
        break;
    }
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
        *r='\t';
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

int parse_buf_to_param(
    char *buf,
    unsigned len
){
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

    int var=0;
    unsigned var_start=0;
    unsigned var_len,var_cnt;

    int ret=0;

    for(unsigned int i=0;i<len&&item_len<PARAM_BUF_ITEM-1&&param_len<PARAM_BUF_SIZE;i++){
        if(var&&!IS_LEGAL(buf[i])){
            var_len=0,var_cnt=0;
            get_var(param+var_start,PARAM_BUF_SIZE-var_start-1,param+var_start+1,&var_len,&var_cnt);
            param_len=var_start+var_cnt;
            var=0;
        }
        if(buf[i]==' '&&!sign){
            if(!skip_flg){
                skip_flg=1;
                param[param_len++]='\0';
            }
            continue;
        }else if(buf[i]=='#'){
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



static inline void insert_str(
    char *buf,
    unsigned pos,
    unsigned len,
    const char *c,
    unsigned c_len
){
    if(len+c_len>=BUF_SIZE){
        return ;
    }
    if(pos==len){
        memcpy(buf+len,c,c_len);
        write(STDOUT_FILENO,c,c_len);
    }else{
        memmove(buf+pos+c_len,buf+pos,len-pos);
        memcpy(buf+pos,c,c_len);
        write(STDOUT_FILENO,buf+pos,len-pos+c_len);
        for(int i=0;i<len-pos;i++){
            write(STDOUT_FILENO,"\b",1);
        }
    }
}

static inline void insert(char *buf,unsigned pos,unsigned len,char c){
    if(pos==len){
        buf[pos]=c;
        write(STDOUT_FILENO,&c,1);
    }else{
        memmove(buf+pos+1,buf+pos,len-pos);
        buf[pos]=c;
        write(STDOUT_FILENO,buf+pos,len-pos+1);
        for(int i=0;i<len-pos;i++){
            write(STDOUT_FILENO,"\b",1);
        }
    }
}

static inline void backspace(char *buf,unsigned pos,unsigned len){
    if(!pos){
        return ;
    }
    if(pos==len){
        write(STDOUT_FILENO,"\b \b",3);
        return ;
    }
    memmove(buf+pos-1,buf+pos,len-pos);
    write(STDOUT_FILENO,"\b",1);
    write(STDOUT_FILENO,buf+pos-1,len-pos);
    write(STDOUT_FILENO," ",1);
    for(int i=0;i<len-pos+1;i++){
        write(STDOUT_FILENO,"\b",1);
    }
}

static inline void delete(char *buf,unsigned pos,unsigned len){
    if(pos>=len){
        return ;
    }
    if(pos==len-1){
        write(STDOUT_FILENO," \b",2);
        return ;
    }
    memmove(buf+pos,buf+pos+1,len-pos);
    write(STDOUT_FILENO,buf+pos,len-pos);
    write(STDOUT_FILENO," ",1);
    for(int i=0;i<len-pos;i++){
        write(STDOUT_FILENO,"\b",1);
    }
}