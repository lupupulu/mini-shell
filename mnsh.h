#ifndef MNSH_H
#define MNSH_H

#include "config.h"

int cmd_strcmp(const char *str1,const char *str2);
long unsigned int cmd_strlen(const char *str);
unsigned cmd_unsigned_to_str(char *str,unsigned long size,unsigned num);
int cmd_execvpe(const char *file, char *const argv[],char *const envp[]);

void history_reset(void);
int history_add(const char *str,unsigned int len);
int history_remove(void);
int get_last_history(char *buf);
int get_next_history(char *buf);

typedef int(*command_func)(char *const *);
typedef struct {
    const char *key;
    command_func f;
    unsigned next;
}hash_pair_t;

extern hash_pair_t builtin_cmd[];
extern hash_pair_t builtin_cmd_hash[HASH_SIZE];
extern char buffer[BUF_SIZE];
extern char const *env_vec[VAR_ITEM];
extern char variable[VAR_ITEM][VAR_SIZE];
extern unsigned char var_umask[VAR_ITEM];
unsigned int hash_function(const char *str);
command_func is_builtin_cmd(char *const *argv);


#define VAR_EXPORT   0b00000001
#define VAR_READONLY 0b00000010
#define VAR_EXIST    0b10000000
unsigned parse_var(char *buf,unsigned len,const char *str);
// int export_var(const char *env,unsigned len);
int get_var(char *buf,unsigned buflen,const char *var,unsigned *len,unsigned *cnt);
int set_var(const char *var,char umask);
int unset_var(const char *var,unsigned len);

const char *get_var_s(const char *var,unsigned len);

int sh_cd(char *const *argv);
int sh_pwd(char *const *argv);
int sh_history(char *const *argv);

int sh_export(char *const *argv);
int sh_readonly(char *const *argv);
int sh_unset(char *const *argv);

int sh_read(char *const *argv);
int sh_echo(char *const *argv);
int sh_printf(char *const *argv);

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

#endif