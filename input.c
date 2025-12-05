#include "mnsh.h"
#include <unistd.h>

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

int input(char *buffer,unsigned bufsiz,unsigned *rlen){
    char c=0;
    int ret=0,con=0;
    unsigned len=0;
    unsigned pos=0;
    while(1){
        ret=read(STDIN_FILENO,&c,1);
        // printf("%d\n",c);
        if(ret==-1){
            return 127;
        }
        if(c==0x04||ret==0){
            write(STDOUT_FILENO,"\nexit\n",6);
            set_terminal_echo(1);
            return -1;
        }
        if(c=='\n'){
            write(STDOUT_FILENO,"\n",1);
            
            if(rlen){
                *rlen=len;
            }
            return 0;
        }else if(c==0x1b){
            ret=handle_escape_sequence();

            if(ret<0) return 127;
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