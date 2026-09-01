#ifndef INPUT_H
#define INPUT_H

void input_set_in(int fd);
void input_set_out(int fd);
#define IN_NO_ECHO        0b01
#define IN_NO_HANDLE_CHAR 0b10
void input_set_mode(unsigned umask);

int input_basic(char **buf,const char *echobuf);
int input(char **buf,const char *echobuf);

char *input_readline(const char *echobuf);
void input_add_history(const char *history);


#ifdef INPUT_IMPLEMENTATION

#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <errno.h>
#include <termios.h>
#define __USE_XOPEN
#include <wchar.h>

#ifndef DARRAY_H

#define darray_t(Tp) \
struct{\
    Tp *arr;\
    size_t size,real;\
}


inline static int da_init(void *array);
inline static int da_add(size_t tp_size,void *array,const void *buf);
inline static int da_resize(size_t tp_size,void *array,size_t size);
inline static int da_fake_pop(size_t tp_size,void *array);
inline static int da_pop(size_t tp_size,void *array);
inline static int da_clear(void *array);

#endif

#ifndef DA_STR_T
#define DA_STR_T
typedef darray_t(char) da_str;
#endif
typedef darray_t(char*) da_history;


static da_history history;
static size_t history_pos;
static da_str input_buffer;
static size_t input_pos;

static int now_is_bufffer=1;
static int in_echo=1;
static int in_fd=STDIN_FILENO;
static int out_fd=STDOUT_FILENO;
static size_t echowidth;

typedef void(*key_func)(void);
typedef struct {
    const char *key;
    key_func func;
    size_t len;
}key_config_t;

static void last_history(void);
static void next_history(void);
static void left(void);
static void right(void);
static void to_start(void);
static void to_end(void);
static void last_word(void);
static void next_word(void);
static void backspace(void);
static void delete(void);
static void clear_left(void);
static void clear_right(void);
static void clear_last_word(void);

static key_config_t key_config[]={
    {"\x01"     , to_start       , 1},
    {"\x02"     , to_start       , 1},
    {"\x06"     , to_end         , 1},
    {"\x08"     , backspace      , 1},
    {"\x0B"     , clear_right    , 1},
    {"\x17"     , clear_last_word, 1},
    {"\x1b[1;5C", next_word      , 6},
    {"\x1b[1;5D", last_word      , 6},
    {"\x1b[3~"  , delete         , 4},
    {"\x1b[A"   , last_history   , 3},
    {"\x1b[B"   , next_history   , 3},
    {"\x1b[C"   , right          , 3},
    {"\x1b[D"   , left           , 3},
    {"\x1b[F"   , to_end         , 3},
    {"\x1b[H"   , to_start       , 3},
    {"\x7f"     , backspace      , 1}
};
static const size_t key_config_size=sizeof(key_config)/sizeof(key_config_t);

#ifndef ECHO_BUF_SIZE
#define ECHO_BUF_SIZE 256
#define UNDEF_ECHO_BUF_SIZE
#endif
static char echobuf[ECHO_BUF_SIZE];
static size_t echobufsiz;

static inline int set_terminal_echo(int enable);
static void echo_to_buf(const char *str,size_t size);
static void echo_buf_to_fd(int fd);

static size_t get_char_width(const char *c);
static int get_char_len(const char *c);

#define output(s,l) if(in_echo)write(out_fd,s,l)
static void insert(const char *c,unsigned len);
static void clean_show(size_t tpos);
static void to_pos(size_t tpos);
static size_t to_pos_next(size_t dpos,size_t tpos);
static size_t to_pos_back(size_t dpos,size_t tpos);
static int deal_keys(da_str *input_buffer,size_t *input_pos,unsigned char c);


static inline int set_terminal_echo(int enable){
    static struct termios original_termios;
    static int is_saved=0;
    struct termios new_termios;

    if(!is_saved){
        if(tcgetattr(in_fd,&original_termios)==-1){
            return 1;
        }
        is_saved=1;
    }

    new_termios=original_termios;

    if(!enable){
        new_termios.c_lflag&=~(ECHO|ICANON|ECHOE|ECHOK|ECHONL);
        new_termios.c_lflag|=TOSTOP;
        new_termios.c_cc[VMIN]=1;
        new_termios.c_cc[VTIME]=0;
    }

    if(tcsetattr(in_fd,TCSANOW,&new_termios)==-1){
        return 1;
    }

    return 0;
}


void input_set_mode(unsigned umask){
    in_echo=!(umask&IN_NO_ECHO);
}
void input_set_in(int fd){
    in_fd=fd;
}
void input_set_out(int fd){
    out_fd=fd;
}

char *input_readline(const char *echobuf){
    char *str=NULL;
    int ret=input(&str,echobuf);
    if(ret==1||ret==-1){
        free(str);
        return NULL;
    }
    return str;
}

void input_add_history(const char *input_buffer){
    size_t size=strlen(input_buffer)+1;
    char *str=malloc(size);
    memcpy(str,input_buffer,size);
    da_add(sizeof(char*),&history,&str);
    history_pos=history.size-1;
}

int input_basic(char **buf,const char *echobuf){
    da_init(&input_buffer);
    input_buffer.arr=*buf;

    if(echobuf){
        size_t len=strlen(echobuf)+1;
        echo_to_buf(echobuf,len);
        echo_buf_to_fd(out_fd);
    }

    char c=0;
    while(1){
        int ret=read(in_fd,&c,1);
        if(ret==-1){
            if(errno==EINTR){
                continue;
            }
            da_add(sizeof(char),&input_buffer,"");
            *buf=input_buffer.arr;
            return -1;
        }
        if(c=='\n'){
            da_add(sizeof(char),&input_buffer,"");
            *buf=input_buffer.arr;
            return 0;
        }else if(ret==0){
            da_add(sizeof(char),&input_buffer,"");
            *buf=input_buffer.arr;
            return 1;
        }
        da_add(sizeof(char),&input_buffer,&c);
    }
}
int input(char **buf,const char *echobuf){
    if(!isatty(in_fd)){
        return input_basic(buf,echobuf);
    }

    da_init(&input_buffer);
    input_buffer.arr=*buf;
    input_pos=0;

    if(echobuf){
        echo_to_buf(echobuf,strlen(echobuf));
        echo_buf_to_fd(out_fd);
        size_t i=0;
        echowidth=0;
        while(echobuf[i]){
            echowidth+=get_char_width(&echobuf[i]);
            int cl=get_char_len(&echobuf[i]);
            i+=cl>0?(size_t)cl:1;
        }
    }else{
        echowidth=0;
    }

    set_terminal_echo(0);
    int ret=0;
    char c;

    history_pos=history.size;

    while(1){
        ret=read(in_fd,&c,1);
        if(ret==-1){
            if(errno==EINTR){
                continue;
            }
            break;
        }
        if(c==0x04||ret==0){
            set_terminal_echo(1);
            da_add(sizeof(char),&input_buffer,"");
            *buf=input_buffer.arr;
            return 1;
        }

        if(c=='\n'){
            echo_to_buf("\n",1);
            echo_buf_to_fd(out_fd);
            if(!now_is_bufffer){
                free(history.arr[history.size-1]);
                da_pop(sizeof(char*),&history);
            }
            now_is_bufffer=1;

            set_terminal_echo(1);
            da_add(sizeof(char),&input_buffer,"");
            *buf=input_buffer.arr;
            return 0;
        }else if(deal_keys(&input_buffer,&input_pos,c)==-1){
            /* 尝试拼出完整多字节字符; 非法/未完成的序列丢弃, 防止越界与死循环 */
            char mb[8]={c};
            size_t i=1;
            int cl;
            while((cl=get_char_len(mb))<=0){
                int rr=read(in_fd,&c,1);
                if(rr<=0){
                    break; /* EOF/读错误 */
                }
                if(c=='\n'){
                    /* 回车结束本行 */
                    echo_to_buf("\n",1);
                    echo_buf_to_fd(out_fd);
                    if(!now_is_bufffer){
                        free(history.arr[history.size-1]);
                        da_pop(sizeof(char*),&history);
                    }
                    now_is_bufffer=1;
                    set_terminal_echo(1);
                    da_add(sizeof(char),&input_buffer,"");
                    *buf=input_buffer.arr;
                    return 0;
                }
                if(i+1>=sizeof(mb)){
                    break; /* 缓冲已满仍无法解析 */
                }
                mb[i++]=c;
            }
            if(cl>0){
                insert(mb,(unsigned)cl);
            }
        }
    }

    set_terminal_echo(1);
    da_add(sizeof(char),&input_buffer,"");
    *buf=input_buffer.arr;
    return -1;
}




#define IS_SHOWN(c) \
    (((c)>=0x20&&(c)<=127)||((c)<0))
#define IS_LEGAL(c) (\
        ((c)>='A'&&(c)<='Z')||\
        ((c)>='a'&&(c)<='z')||\
        ((c)>='0'&&(c)<='9')||\
        (c)=='_'||\
        ((char)c)<0\
    )

int deal_keys(da_str *input_buffer,size_t *input_pos,unsigned char c){
    char buf[16]={c};
    char echo[16]={};
    size_t echo_i=0;
    size_t j=0;
    int ret=0;
    for(size_t i=0;i<16;i++){
        if(i!=0){
            ret=read(in_fd,&buf[i],1);
            if(ret<=0){ /* EOF/读错误: 插入已收集部分 */
                insert(echo,echo_i);
                return 1;
            }
        }
        if(IS_SHOWN(buf[i])){
            echo[echo_i++]=buf[i];
        }
        for(;j<key_config_size;j++){
            if(i==key_config[j].len-1&&buf[i]==key_config[j].key[i]){
                key_config[j].func();
                return 0;
            }
            if(i<key_config[j].len&&buf[i]<=key_config[j].key[i]){
                break;
            }
        }
        if(j==key_config_size||i>=key_config[j].len||buf[i]<key_config[j].key[i]){
            if(echo_i){
                echo_i--;
            }
            insert(echo,echo_i);
            return i==0?-1:1;
        }
    }
    if(echo_i){
        echo_i--;
    }
    insert(echo,echo_i);
    return 1;
}


size_t next_char(const char *str,size_t input_pos,size_t size){
    wchar_t wc;
    if(input_pos>=size){
        return input_pos;
    }
    int r=mbtowc(&wc,str+input_pos,size-input_pos);
    if(r<=0){
        return input_pos+1; /* 非法字节: 前进 1 字节, 保证进展 */
    }
    return input_pos+r;
}

size_t last_char(const char *str,size_t input_pos){
    if(!input_pos){
        return 0;
    }
    wchar_t wc;
    size_t i=1;
    while(input_pos-i&&mbtowc(&wc,str+input_pos-i,MB_CUR_MAX)<0){
        i++;
    }
    return input_pos-i;
}

size_t get_char_width(const char *c){
    wchar_t wc;
    int r=mbtowc(&wc,c,MB_CUR_MAX);
    if(r<0){
        return 0;
    }
    int w=wcwidth(wc);
    return w<0?0:(size_t)w; /* 控制字符等返回 0, 避免 size_t 下溢 */
}

int get_char_len(const char *c){
    wchar_t wc;
    int r=mbtowc(&wc,c,MB_CUR_MAX);
    return r;
}


void insert(const char *c,unsigned len){
    /* 保留冗余(1 个 NUL + 6 字节前瞻), 供 mbtowc 安全读取 */
    if(input_buffer.size+len+6>=input_buffer.real){
        da_resize(sizeof(char),&input_buffer,input_buffer.size+len+1+6);
    }
    memmove(input_buffer.arr+input_pos+len,input_buffer.arr+input_pos,input_buffer.size-input_pos);
    memcpy(input_buffer.arr+input_pos,c,len);
    input_buffer.size+=len;
    input_buffer.arr[input_buffer.size]='\0';
    size_t i=0;
    output(input_buffer.arr+input_pos,input_buffer.size-input_pos);
    size_t dpos=input_pos;
    while(i<len){
        dpos=next_char(input_buffer.arr,dpos,input_buffer.size);
        int cl=get_char_len(&c[i]);
        i+=cl>0?(size_t)cl:1;
    }
    input_pos=input_buffer.size;
    to_pos(dpos);
}

void clean_show(size_t tpos){
    size_t dpos=input_pos;
    size_t cnt=0,width=0;
    while(dpos<input_buffer.size){
        width=get_char_width(&input_buffer.arr[dpos]);
        echo_to_buf("  ",width);
        dpos=next_char(input_buffer.arr,dpos,input_buffer.size);
        cnt+=width;
    }
    while(cnt){
        echo_to_buf("\b",1);
        cnt--;
    }
    if(tpos<input_pos){
        dpos=input_pos;
        if(dpos==input_buffer.size){
            echo_to_buf("\b \b",3);
            dpos=last_char(input_buffer.arr,dpos);
        }
        while(dpos>tpos){
            width=get_char_width(&input_buffer.arr[dpos]);
            echo_to_buf("\b\b",width);
            echo_to_buf("  ",width);
            echo_to_buf("\b\b",width);
            dpos=last_char(input_buffer.arr,dpos);
        }
        input_pos=tpos;
    }
    echo_buf_to_fd(STDOUT_FILENO);

}

void to_pos(size_t tpos){
    if(tpos>input_pos){
        input_pos=to_pos_next(input_pos,tpos);
    }else if(tpos<input_pos){
        input_pos=to_pos_back(input_pos,tpos);
    }
    echo_buf_to_fd(STDOUT_FILENO);
}

size_t to_pos_next(size_t dpos,size_t tpos){
    while(dpos<tpos){
        if(input_buffer.arr[dpos]=='\n'){
            echo_to_buf("\n\r",2);
        }else{
            echo_to_buf("\033[C\033[C",3*get_char_width(&input_buffer.arr[dpos]));
        }
        dpos=next_char(input_buffer.arr,dpos,input_buffer.size);
    }
    return dpos;
}
size_t to_pos_back(size_t dpos,size_t tpos){
    while(dpos>tpos){
        dpos=last_char(input_buffer.arr,dpos);
        if(input_buffer.arr[dpos]=='\n'){
            echo_to_buf("\033[A\r",4);
            size_t i=dpos;
            size_t width=0;
            while(input_buffer.arr[i]!='\n'&&i){
                i=last_char(input_buffer.arr,i);
                width+=get_char_width(&input_buffer.arr[i]);
            }
            if(i==0){
                width+=echowidth;
            }
            for(size_t i=0;i<width;i++){
                echo_to_buf("\033[C",3);
            }
        }else{
            echo_to_buf("\033[D\033[D",3*get_char_width(&input_buffer.arr[dpos]));
        }
    }
    return dpos;
}

void backspace(){
    if(!input_pos){
        return ;
    }
    size_t dpos=last_char(input_buffer.arr,input_pos),opos=input_pos;
    to_pos(dpos);
    clean_show(input_pos);
    memmove(input_buffer.arr+dpos,input_buffer.arr+opos,input_buffer.size-opos);
    input_buffer.size-=opos-dpos;
    input_buffer.arr[input_buffer.size]='\0';
    output(input_buffer.arr+input_pos,input_buffer.size-input_pos);
    input_pos=input_buffer.size;
    to_pos(dpos);
    return ;
}

void delete(){
    if(input_pos>=input_buffer.size){
        return ;
    }

    size_t dpos=next_char(input_buffer.arr,input_pos,input_buffer.size);
    if(input_pos==input_buffer.size-1){
        clean_show(input_pos);
        input_buffer.size-=dpos-input_pos;
        input_buffer.arr[input_buffer.size]='\0';
        return ;
    }

    clean_show(input_pos);
    memmove(input_buffer.arr+input_pos,input_buffer.arr+dpos,input_buffer.size-dpos);
    input_buffer.size-=dpos-input_pos;
    input_buffer.arr[input_buffer.size]='\0';

    output(input_buffer.arr+input_pos,input_buffer.size-input_pos);

    dpos=input_pos;
    input_pos=input_buffer.size;
    to_pos(dpos);

    return ;
}

void left(){
    if(!input_pos){
        return ;
    }
    size_t dpos=last_char(input_buffer.arr,input_pos);
    output("\033[D\033[D",3*get_char_width(&input_buffer.arr[dpos]));
    input_pos=dpos;
    return ;
}

void right(){
    if(input_pos>=input_buffer.size){
        return ;
    }
    size_t dpos=next_char(input_buffer.arr,input_pos,input_buffer.size);
    output("\033[C\033[C",3*get_char_width(&input_buffer.arr[input_pos]));
    input_pos=dpos;
    return ;
}



void to_start(){
    to_pos(0);
    return ;
}

void to_end(){
    to_pos(input_buffer.size);
    return ;
}

void last_word(){
    if(!input_pos){
        return ;
    }
    size_t dpos=last_char(input_buffer.arr,input_pos);
    int legal=IS_LEGAL(input_buffer.arr[dpos]);
    if(!legal){
        while(dpos&&!IS_LEGAL(input_buffer.arr[dpos])){
            dpos=last_char(input_buffer.arr,dpos);
        }
    }
    while(dpos&&IS_LEGAL(input_buffer.arr[dpos])){
        dpos=last_char(input_buffer.arr,dpos);
    }
    if(!IS_LEGAL(input_buffer.arr[dpos])){
        dpos=next_char(input_buffer.arr,dpos,input_buffer.size);
    }
    to_pos(dpos);
    return ;
}

void next_word(){
    if(input_pos>=input_buffer.size){
        return ;
    }
    int legal=IS_LEGAL(input_buffer.arr[input_pos]);
    size_t dpos=input_pos;
    if(!legal){
        while(dpos<input_buffer.size&&!IS_LEGAL(input_buffer.arr[dpos])){
            dpos=next_char(input_buffer.arr,dpos,input_buffer.size);
        }
    }
    while(dpos<input_buffer.size&&IS_LEGAL(input_buffer.arr[dpos])){
        dpos=next_char(input_buffer.arr,dpos,input_buffer.size);
    }
    to_pos(dpos);
    return ;
}

void clear_left(){
    while(input_pos){
        backspace();
    }
    return ;
}

void clear_right(){
    while(input_pos!=input_buffer.size){
        delete();
    }
    return ;
}

void clear_last_word(){
    if(!input_pos){
        return ;
    }
    size_t dpos=last_char(input_buffer.arr,input_pos);
    int legal=IS_LEGAL(input_buffer.arr[dpos]);
    if(!legal){
        while(dpos&&!IS_LEGAL(input_buffer.arr[dpos])){
            dpos=last_char(input_buffer.arr,dpos);
        }
    }
    while(dpos&&IS_LEGAL(input_buffer.arr[dpos])){
        dpos=last_char(input_buffer.arr,dpos);
    }
    if(!IS_LEGAL(input_buffer.arr[dpos])){
        dpos=next_char(input_buffer.arr,dpos,input_buffer.size);
    }
    size_t opos=input_pos;
    to_pos(dpos);
    clean_show(input_pos);
    memmove(input_buffer.arr+dpos,input_buffer.arr+opos,input_buffer.size-opos);
    input_buffer.size-=opos-dpos;
    input_buffer.arr[input_buffer.size]='\0';
    output(input_buffer.arr,input_buffer.size-input_pos);
    input_pos=input_buffer.size;
    to_pos(dpos);
    return ;
}

void last_history(){
    if(!history_pos){
        return ;
    }

    clean_show(0);

    if(now_is_bufffer){
        char *p=malloc(sizeof(char)*(input_buffer.size+1));
        if(input_buffer.size){
            memcpy(p,input_buffer.arr,input_buffer.size);
        }
        p[input_buffer.size]='\0';
        da_add(sizeof(char*),&history,&p);
        now_is_bufffer=0;
    }
    history_pos--;
    da_clear(&input_buffer);
    input_buffer.size=strlen(history.arr[history_pos]);
    input_buffer.arr=malloc(input_buffer.size+7);
    memcpy(input_buffer.arr,history.arr[history_pos],input_buffer.size);
    input_buffer.arr[input_buffer.size]='\0';
    input_buffer.real=input_buffer.size+7;
    input_pos=input_buffer.size;

    output(input_buffer.arr,input_buffer.size);
    return ;
}

void next_history(){
    if(!history.size||history_pos>=history.size-1){
        return ;
    }

    clean_show(0);

    history_pos++;
    da_clear(&input_buffer);
    input_buffer.size=strlen(history.arr[history_pos]);
    input_buffer.arr=malloc(input_buffer.size+7);
    memcpy(input_buffer.arr,history.arr[history_pos],input_buffer.size);
    input_buffer.arr[input_buffer.size]='\0';
    input_buffer.real=input_buffer.size+7;
    input_pos=input_buffer.size;
    if(history_pos==history.size-1){
        free(history.arr[history.size-1]);
        da_pop(sizeof(char*),&history);
        now_is_bufffer=1;
    }

    output(input_buffer.arr,input_buffer.size);
    return ;
}


void echo_to_buf(const char *str,size_t size){
    if(!in_echo){
        return ;
    }

    if(size>=ECHO_BUF_SIZE){
        echo_buf_to_fd(out_fd);
        write(out_fd,str,size);
    }else if(echobufsiz+size>=ECHO_BUF_SIZE){
        echo_buf_to_fd(out_fd);
        memcpy(echobuf,str,size);
        echobufsiz+=size;
    }else{
        memcpy(echobuf+echobufsiz,str,size);
        echobufsiz+=size;
    }
}

void echo_buf_to_fd(int fd){
    if(!in_echo){
        return ;
    }

    write(out_fd,echobuf,echobufsiz);
    echobufsiz=0;
}


#ifndef DARRAY_H

int da_init(void *array){
    memset(array,0,sizeof(darray_t(void)));
    return 0;
}

int da_add(size_t tp_size,void *array,const void *buf){
    darray_t(void) *arr=array;
    if(arr->size+1>=arr->real){
        void *p=realloc(arr->arr,(arr->real*3/2+1)*tp_size);
        if(!p){
            return 127;
        }
        arr->arr=p;
        arr->real=arr->real*3/2+1;
    }
    memcpy(arr->arr+arr->size*tp_size,buf,tp_size);
    arr->size++;
    return 0;
}

int da_resize(size_t tp_size,void *array,size_t size){
    darray_t(void) *arr=array;
    void *p=realloc(arr->arr,size*tp_size);
    if(!p){
        return 127;
    }
    arr->arr=p;
    arr->real=size;
    if(arr->real<arr->size){
        arr->size=arr->real;
    }
    return 0;
}

int da_fake_pop(size_t tp_size,void *array){
    darray_t(void) *arr=array;
    if(arr->size<arr->real/2){
        void *p=realloc(arr->arr,arr->real/2*tp_size);
        if(p){
            arr->arr=p;
            arr->real/=2;
        }
    }
    return 0;
}

int da_pop(size_t tp_size,void *array){
    darray_t(void) *arr=array;
    if(!arr->size){
        return 0;
    }
    arr->size--;
    return da_fake_pop(tp_size,array);
}

int da_clear(void *array){
    darray_t(void) *arr=array;
    free(arr->arr);
    memset(arr,0,sizeof(darray_t(void)));
    return 0;
}

#endif


#undef output
#undef IS_SHOWN
#undef IS_LEGAL

#ifdef UNDEF_ECHO_BUF_SIZE
#undef UNDEF_ECHO_BUF_SIZE
#endif

#endif

#endif