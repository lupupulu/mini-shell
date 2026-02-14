#ifndef MNSH_H
#define MNSH_H

#include <stddef.h>

#define IS_LEGAL(c) (\
        ((c)>='A'&&(c)<='Z')||\
        ((c)>='a'&&(c)<='z')||\
        ((c)>='0'&&(c)<='9')||\
        (c)=='_'\
    )

#define IS_SPECIAL_VARIABLE(c) \
    ((c)=='$' || (c)=='!' || (c)=='?' || (c)=='-' ||\
    (c)=='*' || (c)=='@' || (c)=='#')

#define IS_SHOWN(c) \
    (((c)>=0x20&&(c)<=127)||((c)<0))


#define MIN(a,b) ((a)<(b)?(a):(b))

#define darray_t(Tp) \
struct{\
    Tp *arr;\
    size_t size,real;\
}

int da_init(void *array);
int da_add(size_t tp_size,void *array,const void *buf);
int da_resize(size_t tp_size,void *array,size_t size);
int da_fake_pop(size_t tp_size,void *array);
int da_pop(size_t tp_size,void *array);
int da_fake_clear(void *array);
int da_clear(void *array);

extern int g_argc;
#define MAX_LL_SIZE 21
extern char g_argc_s[MAX_LL_SIZE];
extern char* const* g_argv;
extern int g_ret;
extern char g_ret_s[MAX_LL_SIZE];
extern int g_pid;
extern char g_pid_s[MAX_LL_SIZE];

extern int now_pid;
extern char *now_name;

extern int is_script;
extern int is_child;

int set_terminal_echo(int enable);
void set_signal_handler(int enable);

typedef struct {
    char **argv;
    char *cmds;
    size_t argvn;
    size_t cmdlen;
}command_t;
typedef darray_t(command_t) da_command;

size_t next_char(const char *str,size_t pos,size_t size);
size_t last_char(const char *str,size_t pos);
size_t get_char_width(const char *c);
size_t get_char_len(char c);

size_t utf8_get_char_width(const char *c);

#define ECHO_BUF_SIZE 256
void echo_unsigned_to_buf(size_t num);
void echo_to_buf(const char *str,size_t size);
void echo_buf_to_fd(int fd);

unsigned cmd_unsigned_to_str(char *str,unsigned long size,unsigned num);
unsigned cmd_str_to_unsigned(const char *str,unsigned long size);
int cmd_execvpe(const char *file, char *const argv[],char *const envp[]);

const char *file_is_exist(const char *file,int type,int is_cmd);
int is_variable(const char *cmd);

#define UTF8_4_CHECK_UMASK 0b11111000
#define UTF8_4_UMASK       0b11110000
#define UTF8_3_CHECK_UMASK 0b11110000
#define UTF8_3_UMASK       0b11100000
#define UTF8_2_CHECK_UMASK 0b11100000
#define UTF8_2_UMASK       0b11000000
#define UTF8_1_CHECK_UMASK 0b11000000
#define UTF8_1_UMASK       0b10000000
#define UTF8_CHECK(c,n) (((c)&UTF8_##n##_CHECK_UMASK)==UTF8_##n##_UMASK)
typedef enum{
    C,
    UTF8
}codeset_t;
extern codeset_t codeset;

typedef darray_t(char) da_str;
extern da_str buffer;
typedef darray_t(char*) da_history;
extern da_history history;
extern size_t history_pos;

#define IN_ECHO        0b01
#define IN_HANDLE_CHAR 0b10
int input_basic(void);
int input(unsigned umask);

#define REDIR_IN              1
#define REDIR_OUT             2
#define REDIR_OUT_ADD         3
#define REDIR_DUP             4
#define REDIR_HERE_DOCUMENT   5
#define REDIR_HERE_STRING     6
#define REDIR_CLOSE           7
int is_redirector(const char *cmd,size_t *inter,int *a);



#define HASH_SIZE 63

typedef int(*command_func)(char *const *);
typedef struct {
    const char *key;
    command_func f;
    unsigned next;
}hash_pair_t;

extern hash_pair_t builtin_cmd[];
extern hash_pair_t builtin_cmd_hash[];
unsigned int hash_function(const char *str);
#define MAKE_HASH_FUNCTION \
unsigned int hash_function(const char *str){\
    if(!str||!*str)return 0;\
    unsigned int hash=(unsigned char)*str;\
    int len=1;\
    while(str[len]&&len<6){\
        hash=(hash<<3)^(unsigned char)str[len];\
        len++;\
    }\
    return (hash+len)%HASH_SIZE;\
}
command_func is_builtin_cmd(char *const *argv);



typedef struct{
    char *var;
    size_t eq_loc;
    unsigned umask;
    unsigned env;
}variable_t;
typedef darray_t(variable_t) da_variable;
extern da_variable variable;
extern da_variable tmp_env;
typedef darray_t(char*) da_env;
extern da_env env;

#define VAR_EXPORT   0b00000001
#define VAR_READONLY 0b00000010
#define VAR_ARRAY    0b00000100
#define VAR_EXIST    0b10000000
variable_t *find_var(const char *var);
const char *get_var(const char *var);
int set_var(const char *var,char umask);
int unset_var(const char *var);
size_t set_env(char *str);
void unset_env(size_t i);
int set_tmp_env(char *str);
int recovery_tmp_env(void);

typedef struct{
    char *var;
    size_t eq_loc;
}alias_t;
typedef darray_t(alias_t) da_alias;
extern da_alias alias;
alias_t *find_alias(const char *als);
const char *get_alias(const char *als);
int set_alias(const char *als);
int unset_alias(const char *als);

#define JOB_RUNNING     0
#define JOB_STOPPED     1
#define JOB_OUT_STOPPED 2
#define JOB_IN_STOPPED  3
#define JOB_FINISHED    4
typedef struct {
    char *name;
    size_t num;
    int pid;
    int stat;
}job_t;
typedef darray_t(job_t) da_job;
extern da_job job;
char *restore_cmd(command_t *cmds,size_t size);
int add_job(char *name,int pid,int stat);
size_t find_job_pid(int pid);
size_t find_job_num(size_t num);
size_t get_job_num(const char *str);
int del_job_pid(int pid);

typedef struct {
    int pid;
    int stat;
}jobmsg_t;
#define JOB_MSG_SIZE 64
extern jobmsg_t jobmsg[JOB_MSG_SIZE];
extern size_t jobmsgsiz;

#define MAX_JOB_OUT_TIMES 4
int deal_jobmsg(void);

void sig_int_handler(int sig);
void sig_chld_handler(int sig);
void sig_tstp_handler(int sig);
void sig_cont_handler(int sig);
void sig_ttou_hangler(int sig);

#define PATH_BUF_SIZE 4096
extern char pathbuf[PATH_BUF_SIZE];

int sh_cd(char *const *argv);
int sh_pwd(char *const *argv);
int sh_history(char *const *argv);

int sh_export(char *const *argv);
int sh_readonly(char *const *argv);
int sh_unset(char *const *argv);

int sh_read(char *const *argv);
int sh_echo(char *const *argv);

int sh_jobs(char *const *argv);
int sh_fg(char *const *argv);
int sh_bg(char *const *argv);
int sh_wait(char *const *argv);

int sh_test(char *const *argv);
int sh_true(char *const *argv);
int sh_false(char *const *argv);

int sh_command(char *const *argv);
int sh_exec(char *const *argv);
int sh_eval(char *const *argv);
int sh_times(char *const *argv);

int sh_trap(char *const *argv);
int sh_set(char *const *argv);
int sh_shift(char *const *argv);
int sh_getopts(char *const *argv);

int sh_umask(char *const *argv);
int sh_alias(char *const *argv);
int sh_unalias(char *const *argv);
int sh_type(char *const *argv);



typedef int(*key_func)(void);

int last_history(void);
int next_history(void);
int left(void);
int right(void);
int to_start(void);
int to_end(void);
int last_word(void);
int next_word(void);
int backspace(void);
int delete(void);
int clear_left(void);
int clear_right(void);
int clear_last_word(void);

typedef struct {
    char key;
    const char *esc; // pls ensure that the length of this value is <= 16
    const char *func;
}key_setting_t;

typedef struct {
    unsigned next;
    unsigned len;
    const char *esc;
    key_func func;
}key_setting_list_t;

#define CHAR_MAX 256

extern key_setting_list_t key_config[CHAR_MAX];
extern key_setting_list_t key_config_list[];

#endif
