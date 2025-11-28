#include "mnsh.h"
#include <memory.h>
#include <unistd.h>
#include <stdio.h>

#define MIN(a,b) ((a)<(b)?(a):(b))

char variable[VAR_ITEM][VAR_SIZE];
static char var_umask[VAR_ITEM];
static unsigned var_size;
char *env_vec[VAR_ITEM];
static unsigned env_size;

static inline int find_var(unsigned *loc,const char *var,int env);
// static inline int find_var_for_get(unsigned *loc,const char *var,int len);
static inline int unset(unsigned loc);

static inline int var_strcmp(const char *str1,const char *str2);
// static inline int var_strcmp_for_get(const char *str1,const char *str2,unsigned len);

unsigned parse_var(char *buf,unsigned len,const char *str){
    unsigned cnt=0,l=0,c=0;
    unsigned i=0;
    if(str[0]=='~'&&(!str[1]||str[1]=='/')){
        if(get_var(buf,len-cnt,"$HOME",&l,&c)>0){
            cnt+=c;
        }
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
            for(unsigned i=0;i<var_size;i++){
                printf("%u %s\n",i,env_vec[i]);
            }
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
    int ret=0;
    unsigned loc=0,cnt=0;
    // ret=find_var_for_get(&loc,var,len);
    if(ret){
        cnt=MIN(cmd_strlen(variable[loc]),buflen-len);
        memcpy(buf,variable[loc],cnt);
        if(*c){
            *c=cnt;
        }
        return var_umask[loc];
    }
    if(*c){
        *c=cnt;
    }
    return 0;
}
int set_var(const char *var,unsigned len,char umask){
    unsigned loc=0;
    int ret=find_var(&loc,var,umask&VAR_EXPORT);
    if(ret){
        if(var_umask[loc]&VAR_READONLY){
            return 1;
        }
        if((umask^var_umask[loc])&VAR_EXPORT){
            unset(loc);
            return set_var(var,len,umask);
        }
        var_umask[loc]|=umask;
        parse_var(env_vec[loc],VAR_SIZE,var);
        // printf("1 %d %s\n",loc,env_vec[loc]);
        if(umask&VAR_EXPORT){
            env_size++;
        }
        var_size++;
        return 0;
    }
    unsigned i=0;
    for(;i<VAR_ITEM;i++){
        if(!(var_umask[i]&VAR_EXIST)){
            break;
        }
    }
    if(i>=VAR_ITEM){
        return 2;
    }
    var_umask[i]=umask|VAR_EXIST;
    memcpy(&variable[i][0],var,MIN(cmd_strlen(var),VAR_SIZE));
    if(umask&VAR_EXPORT){
        memmove(env_vec+loc+1,env_vec+loc,sizeof(char*)*(var_size-loc+1));
        env_vec[loc]=&variable[i][0];
        env_size++;
    }else{
        memmove(env_vec+loc+1,env_vec+loc,sizeof(char*)*(var_size-loc));
        env_vec[loc]=&variable[i][0];
    }
    // printf("%s\n",env_vec[loc]);
    var_size++;
    // for(unsigned i=0;i<var_size;i++){
    //     printf("        %u %s\n",i,env_vec[i]);
    // }
    return 0;
}
int unset_var(const char *var,unsigned len){
    unsigned loc=0;
    int ret=find_var(&loc,var,1);
    if(!ret){
        ret=find_var(&loc,var,0);
        if(!ret){
            return 1;
        }
    }
    if(ret&VAR_READONLY){
        return 1;
    }
    unset(loc);
    return 0;
}

static inline int find_var(unsigned *loc,const char *var,int env){
    unsigned start=env?0:env_size;
    unsigned end=env?env_size:var_size;

    if(start>=end){
        *loc=start;
        return 0;
    }
    unsigned l=start,r=end;
    unsigned mid=0;
    while(l<r){
        mid=(l+r)/2;
        int cmp=var_strcmp(env_vec[mid],var);
        if(cmp==0){
            *loc=mid;
            return var_umask[mid];
        }else if(cmp>0){
            l=mid+1;
        }else{
            r=mid;
        }
    }
    if(!mid){
        *loc=0;
    }else{
        *loc=mid+1;
    }
    return 0;
}

static inline int unset(unsigned loc){
    if(*(env_vec[loc]-1)&VAR_READONLY){
        return 1;
    }
    *(env_vec[loc]-1)=0;
    if(loc<env_size){
        memmove(env_vec+loc,env_vec+loc+1,sizeof(char*)*(var_size-loc+1));
    }else{
        memmove(env_vec+loc+1,env_vec+loc+2,sizeof(char*)*(var_size-loc+2));
    }
    var_size--;
    if(loc<env_size){
        env_size--;
    }
    return 0;
}

static inline int var_strcmp(const char *str1,const char *str2){
    if(!str1){
        return -1;
    }
    if(!str2){
        return 1;
    }
    unsigned len=0;
    char l1,l2;
    while(str1[len]==str2[len]&&str1[len]!=' '&&str2[len]!=' '&&str1[len]!='='&&str2[len]!='='){
        len++;
    }
    l1=str1[len];
    l2=str2[len];
    if(l1=='='){
        l1='\0';
    }
    if(l2=='='){
        l2='\0';
    }
    if(l1>l2){
        return 1;
    }else if(l1<l2){
        return -1;
    }
    return 0;
}
