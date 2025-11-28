#include "mnsh.h"
#include <unistd.h>
#include <memory.h>
// #include <string.h>
// #include <stdio.h>

#define MIN(a,b) ((a)<(b)?(a):(b))

static char history[HISTORY_BUF_SIZE];
static unsigned int history_len;
static unsigned int pos;

void history_reset(void){
    history_len=0;
    pos=0;
}

int history_add(const char *str,unsigned int len){
    // unsigned len=strlen(str);
    if(!str)return 1;
    if(history_len+len+3>=HISTORY_BUF_SIZE){
        // return 1;
        history_reset();
    }
    memcpy(history+history_len,str,len);
    pos=history_len;
    history_len+=len;
    history[history_len++]='\0';
    *(unsigned short*)(history+history_len)=len;
    history_len+=2;

    return 0;
}

int history_remove(void){
    if(history_len<2){
        return 1;
    }
    unsigned short len=*(unsigned short*)(history+history_len-2);
    history_len-=(len+3);
    if(pos>history_len){
        if(!history_len){
            pos=0;
            return 0;
        }
        len=*(unsigned short*)(history+history_len-2);
        pos=history_len-len-3;
    }
    return 0;
}

int get_last_history(char *buf){
    if(pos==0){
        // printf("%d %d\n",pos,history_len);
        return 1;
    }
    unsigned short len=*(unsigned short*)(history+pos-2);
    pos-=(len+3);
    if(buf){
        memcpy(buf,history+pos,len+1);
    }
    // printf("%d %d\n",pos,history_len);
    // sh_history(NULL);
    return 0;
}

int get_next_history(char *buf){
    if(pos==history_len){
        // printf("%d %d\n",pos,history_len);
        return 1;
    }
    if(pos==history_len-(*(unsigned short*)(history+history_len-2))-3){
        return 1;
    }
    unsigned int i=0;
    while(history[pos+i])i++;
    pos+=i+3;
    i=0;
    while(history[pos+i]){
        if(buf){
            buf[i]=history[pos+i];
        }
        i++;
    }
    if(buf){
        buf[i]='\0';
    }
    // printf("%d %d\n",pos,history_len);
    // sh_history(NULL);
    if(pos+i+3==history_len){
        return 1;
    }
    return 0;
}


int sh_history(char *const *argv){
    unsigned int i=0,cnt=0,len=0,tmp=0;
    // printf("%d\n",history_len);
    while(i<history_len){
        buffer[0]=' ';
        buffer[1]=' ';
        len=2;
        len+=cmd_unsigned_to_str(buffer+2,BUF_SIZE-2,cnt);
        buffer[len++]=' ';
        buffer[len++]=' ';

        tmp=strlen(history+i);
        memcpy(buffer+len,history+i,MIN(tmp,BUF_SIZE-len-1));
        len+=MIN(tmp,BUF_SIZE-len-1);
        buffer[len++]='\n';

        write(STDOUT_FILENO,buffer,len);
        i+=strlen(history+i);
        i+=3;
        cnt++;
    }
    return 0;
}

