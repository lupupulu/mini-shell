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
// typedef void(*da_copy_func)(void *target,void *src);

int set_terminal_echo(int enable);

size_t next_char(const char *str,size_t size,size_t pos);
size_t last_char(const char *str,size_t size,size_t pos);

unsigned cmd_unsigned_to_str(char *str,unsigned long size,unsigned num);
unsigned cmd_str_to_unsigned(const char *str,unsigned long size);
int cmd_execvpe(const char *file, char *const argv[],char *const envp[]);

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
int input(unsigned umask);


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

// extern darray_t(char) buffer;


typedef struct{
    char *var;
    size_t eq_loc;
    unsigned umask;
    unsigned env;
}variable_t;
typedef darray_t(variable_t) da_variable;
extern da_variable variable;
typedef darray_t(char*) da_env;
extern da_env env;

#define VAR_EXPORT   0b00000001
#define VAR_READONLY 0b00000010
#define VAR_ARRAY    0b00000100
#define VAR_EXIST    0b10000000
unsigned parse_var(char *buf,unsigned len,const char *str);
variable_t *find_var(const char *var);
const char *get_var(const char *var,size_t i);
int set_var(const char *var,char umask);
int unset_var(const char *var);
size_t set_env(char *str);
void unset_env(size_t i);

#define PATHBUFSIZ 4096
extern char pathbuf[PATHBUFSIZ];

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
