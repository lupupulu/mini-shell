#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define BUFFER_SIZE 8192
char buffer[BUFFER_SIZE];
#define ARG_SIZE 64
char *arg[ARG_SIZE];

static size_t readaline(char *buf,size_t bufsiz);
static int parse(int max_argc,char **argv,char *buf,size_t len);
static int run(int argc,char **argv);

static char *parse_item(char *buf,size_t *i,size_t len);
static char *parse_quote(char *buf,size_t *i,size_t len);

int main(int argc,char **argv){
    int r=0;
    while(1){
        printf("%d run: ",r);
        size_t len=readaline(buffer,BUFFER_SIZE);
        if(len==0&&buffer[0]==127){
            break;
        }
        size_t argcnt=parse(ARG_SIZE,arg,buffer,len);
        r=run(argcnt,arg);
    }
    return 0;
}

static size_t readaline(char *buf,size_t bufsiz){
    size_t i=0;
    for(;i<bufsiz-1;i++){
        int c=fgetc(stdin);
        if(c=='\n'||c=='\0'){
            break;
        }else if(c==EOF){
            buf[0]=127;
            return 0;
        }
        buf[i]=c;
    }
    buf[i]='\0';
    return i;
}

static int parse(int max_argc,char **argv,char *buf,size_t len){
    size_t argc=0;
    size_t i=0;
    for(;buf[i]==' '&&i<len;i++){}
    while(i<len&&argc<max_argc-1){
        char *start=NULL;
        if(buf[i]=='\"'||buf[i]=='\''){
            start=parse_quote(buf,&i,len);
        }else{
            start=parse_item(buf,&i,len);
        }
        argv[argc++]=start;
        for(;buf[i]==' '&&i<len;i++){}
    }
    argv[argc]=NULL;
    return argc;
}

static int run(int argc,char **argv){
    int pid=fork();
    if(pid==-1){
        return -1;
    }else if(pid==0){
        exit(execvp(argv[0],argv));
    }
    int status=0;
    waitpid(pid,&status,0);
    if(WIFEXITED(status)){
        return WEXITSTATUS(status);
    }else if(WIFSIGNALED(status)){
        return -2;
    }
    return -3;
}

static char *parse_item(char *buf,size_t *i,size_t len){
    char *ret=&buf[*i];
    for(;buf[*i]!=' '&&*i<len;(*i)++){}
    buf[(*i)++]='\0';
    return ret;
}

static char *parse_quote(char *buf,size_t *i,size_t len){
    char quote=buf[(*i)++];
    char *ret=&buf[*i];
    for(;buf[*i]!=quote&&*i<len;(*i)++){}
    buf[(*i)++]='\0';
    return ret;
}
