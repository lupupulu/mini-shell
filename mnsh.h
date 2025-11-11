#ifndef MNSH_H
#define MNSH_H

#include "config.h"

typedef int(*command_func)(char *const *);
typedef struct {
    const char *key;
    command_func f;
    unsigned next;
}hash_pair_t;

extern hash_pair_t builtin_cmd[];
extern hash_pair_t builtin_cmd_hash[HASH_SIZE];
extern char buffer[BUF_SIZE];
unsigned int hash_function(const char *str);
command_func is_builtin_cmd(char *const *argv);

int sh_cd(char *const *argv);
int sh_pwd(char *const *argv);

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