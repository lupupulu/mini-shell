#include "mnsh.h"
#include <memory.h>
#include <unistd.h>
#include <stdio.h>

#define MIN(a,b) ((a)<(b)?(a):(b))

char variable[VAR_ITEM][VAR_SIZE];
unsigned char var_umask[VAR_ITEM];
char const *env_vec[VAR_ITEM];
static unsigned env_size;

static inline unsigned find_var(const char *var,unsigned len);
static inline int unset_env(const char *env);
static inline int new_env(const char *env);

static inline int var_strncmp(const char *str1,const char *str2,unsigned max_len);

unsigned parse_var(char *buf,unsigned len,const char *str){
    unsigned cnt=0,l=0,c=0;
    unsigned i=0;
    if(str[0]=='~'&&(!str[1]||str[1]=='/')){
        get_var(buf,len,"HOME",&l,&c);
        cnt+=c;
        i++;
    }

    char sign=0;
    while(str[i]&&cnt<len){
        if(str[i]=='\''){
            if(!sign){
                sign='\'';
            }else{
                sign=0;
            }
        }else if(str[i]=='$'&&!sign){
            // for(unsigned i=0;i<env_size;i++){
            //     printf("%u %s\n",i,env_vec[i]);
            // }
            get_var(buf+cnt,len-cnt,str+i+1,&l,&c);
            cnt+=c;
            i+=l+1;
            continue;
        }
        buf[cnt]=str[i];
        cnt++;
        i++;
    }

    buf[cnt]='\0';
    return cnt;
}

int get_var(char *buf,unsigned buflen,const char *var,unsigned *l,unsigned *c){
    unsigned len=0,rl=0;
    if(var[0]=='{'){
        var=var+1;
        rl++;
        while(var[len]&&var[len]!='}'){
            len++;
        }
        rl+=len;
        if(l){
            *l=rl;
        }
        if(!var[len]){
            return -1;
        }
        rl++;
    }else{
        while(
            (var[len]>='A'&&var[len]<='Z')||
            (var[len]>='a'&&var[len]<='z')||
            (var[len]>='0'&&var[len]<='9')||
            var[len]=='_'
        ){
            len++;
        }
        rl+=len;
    }
    if(l){
        *l=rl;
    }
    unsigned ret=-1;
    unsigned cnt=0,start=0;
    ret=find_var(var,len);
    // printf("%s %u %u\n",var,len,ret);
    if(ret!=(unsigned)-1){
        while(variable[ret][start]!='='){
            start++;
        }
        start++;
        cnt=MIN(cmd_strlen(variable[ret]+start),buflen-len);
        memmove(buf,variable[ret]+start,cnt);
        if(c){
            *c=cnt;
        }
        return var_umask[ret];
    }
    if(c){
        *c=cnt;
    }
    return 0;
}
int set_var(const char *var,char umask){
    int flg=0;
    unsigned len=0;
    while(len<BUF_SIZE&&var[len]){
        if(var[len]=='='){
            flg=1;
        }
        len++;
    }

    unsigned loc=find_var(var,len);
    if(loc==(unsigned)-1){
        if(!flg){
            return 2;
        }
        for(unsigned i=0;i<VAR_ITEM;i++){
            if(!(var_umask[i]&VAR_EXIST)){
                loc=i;
                break;
            }
        }
    }else if(var_umask[loc]&VAR_READONLY){
        return 3;
    }

    if(loc==(unsigned)-1){
        return 4;
    }

    if(!flg){
        var_umask[loc]|=umask;
        return 0;
    }

    memcpy(variable[loc],var,len);
    if(var_umask[loc]&VAR_EXPORT&&!umask&VAR_EXPORT){
        unset_env(variable[loc]);
    }else if(!var_umask[loc]&VAR_EXPORT&&umask&VAR_EXPORT){
        new_env(variable[loc]);
    }
    var_umask[loc]=VAR_EXIST|umask;

    return 0;
}
int unset_var(const char *var,unsigned len){
    unsigned loc=find_var(var,cmd_strlen(var));
    if(loc==(unsigned)-1){
        return 1;
    }else if(var_umask[loc]&VAR_READONLY){
        return 2;
    }
    if(var_umask[loc]&VAR_EXPORT){
        unset_env(variable[loc]);
    }
    var_umask[loc]=0;
    return 0;
}

const char *get_var_s(const char *var,unsigned len){
    unsigned r=find_var(var,len);
    if(r==(unsigned)-1){
        return NULL;
    }
    return variable[r];
}

static inline unsigned find_var(const char *var,unsigned len){
    for(unsigned i=0;i<VAR_ITEM;i++){
        if(var_umask[i]&VAR_EXIST&&!var_strncmp(var,variable[i],len)){
            return i;
        }
    }
    return -1;
}
static inline int unset_env(const char *env){
    for(unsigned i=0;i<VAR_ITEM;i++){
        if(env==env_vec[i]){
            if(var_umask[i]&VAR_READONLY){
                return 2;
            }
            memmove(env_vec+i,env_vec+i+1,sizeof(char*)*(env_size-i));
            env_size--;
            return 0;
        }
    }
    return 1;
}

static inline int new_env(const char *env){
    if(env_size+1>=VAR_ITEM){
        return 1;
    }
    env_vec[env_size++]=env;
    env_vec[env_size]=NULL;
    return 0;
}

static inline int var_strncmp(const char *str1,const char *str2,unsigned max_len){
    if(!str1){
        return -1;
    }
    if(!str2){
        return 1;
    }
    unsigned len=0;
    char l1,l2;
    while(len<max_len&&str1[len]==str2[len]&&str1[len]!=' '&&str2[len]!=' '&&str1[len]!='='&&str2[len]!='='){
        len++;
    }
    l1=str1[len];
    l2=str2[len];
    if(l1=='='||len==max_len){
        l1='\0';
    }
    if(l2=='='||len==max_len){
        l2='\0';
    }
    if(l1>l2){
        return 1;
    }else if(l1<l2){
        return -1;
    }
    return 0;
}
