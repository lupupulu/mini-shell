#include "mnsh.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

hash_pair_t builtin_cmd[HASH_SIZE];
unsigned int extra_builtin_real=1;
hash_pair_t builtin_cmd_hash[HASH_SIZE];

codeset_t codeset;
da_str buffer;

key_setting_list_t key_config[CHAR_MAX];
key_setting_list_t key_config_list[256];
unsigned list_size=1;


int register_builtin(const char *name);
void init(void);

MAKE_HASH_FUNCTION


void init(void){
    register_builtin("cd");
    register_builtin("pwd");
    register_builtin("history");

    register_builtin("export");
    register_builtin("readonly");
    register_builtin("unset");

    register_builtin("read");
    register_builtin("echo");

    register_builtin("jobs");
    register_builtin("fg");
    register_builtin("bg");
    register_builtin("wait");

    register_builtin("test");
    register_builtin("true");
    register_builtin("false");

    register_builtin("command");
    register_builtin("exec");
    register_builtin("eval");
    register_builtin("times");

    register_builtin("trap");
    register_builtin("set");
    register_builtin("shift");
    register_builtin("getopts");

    register_builtin("umask");
    register_builtin("alias");
    register_builtin("unalias");
    register_builtin("type");

    for(unsigned i=0;i<sizeof(key_setting)/sizeof(key_setting_t);i++){
        key_setting_t *p=&key_setting[i];
        size_t len=0;
        if(p->esc){
            len=strlen(p->esc);
        }
        key_setting_list_t new={.esc=p->esc,.len=len,.func=(void*)p->func,.next=0};
        if(!key_config[(unsigned)p->key].func){
            key_config[(unsigned)p->key]=new;
            continue;
        }
        key_setting_list_t *now=&key_config[(unsigned)p->key];

        while(len>now->len){
            if(!now->next){
                key_config_list[list_size]=new;
                now->next=list_size;
                goto L;
            }
            now=&key_config_list[now->next];
        }
        key_config_list[list_size]=*now;
        new.next=list_size;
        *now=new;
        L:list_size++;
    }

    return ;
}

int main(int argc,const char *argv[]){
    init();

    FILE *fp=fopen("config.c","w");

    fprintf(fp,"#include \"mnsh.h\"\n\n");

    fprintf(fp,"hash_pair_t builtin_cmd[%u]={\n",extra_builtin_real);
    fprintf(fp,"\t{},\n");
    for(int i=1;i<extra_builtin_real;i++){
        fprintf(fp,"\t{.key=\"%s\",.f=sh_%s,.next=%u}",builtin_cmd[i].key,builtin_cmd[i].key,builtin_cmd[i].next);
        if(i<extra_builtin_real-1){
            fprintf(fp,",\n");
        }else{
            fprintf(fp,"\n");
        }
    }
    fprintf(fp,"};\n\n");

    fprintf(fp,"hash_pair_t builtin_cmd_hash[HASH_SIZE]={\n");
    for(int i=0;i<HASH_SIZE;i++){
        if(builtin_cmd_hash[i].key==NULL){
            fprintf(fp,"\t{}");
        }else{
            fprintf(fp,"\t{.key=\"%s\",.f=sh_%s,.next=%u}",builtin_cmd_hash[i].key,builtin_cmd_hash[i].key,builtin_cmd_hash[i].next);
        }
        if(i<HASH_SIZE-1){
            fprintf(fp,",\n");
        }else{
            fprintf(fp,"\n");
        }
    }
    fprintf(fp,"};\n\n");

    fprintf(fp,"key_setting_list_t key_config[%d]={\n",CHAR_MAX);
    for(unsigned i=0;i<CHAR_MAX;i++){
        if(!key_config[i].func){
            fprintf(fp,"\t{}");
        }else if(key_config[i].esc){
            fprintf(fp,"\t{.next=%u,.len=%u,.esc=\"%s\",.func=%s}",key_config[i].next,key_config[i].len,key_config[i].esc,(char*)key_config[i].func);
        }else{
            fprintf(fp,"\t{.next=%u,.len=0,.esc=NULL,.func=%s}",key_config[i].next,(char*)key_config[i].func);
        }
        if(i<CHAR_MAX-1){
            fprintf(fp,",\n");
        }else{
            fprintf(fp,"\n");
        }
    }
    fprintf(fp,"};\n\n");

    fprintf(fp,"key_setting_list_t key_config_list[]={\n");
    fprintf(fp,"\t{},\n");
    for(unsigned i=1;i<list_size;i++){
        if(key_config_list[i].esc){
            fprintf(fp,"\t{.next=%u,.len=%u,.esc=\"%s\",.func=%s}",key_config_list[i].next,key_config_list[i].len,key_config_list[i].esc,(char*)key_config_list[i].func);
        }else{
            fprintf(fp,"\t{.next=%u,.len=0,.esc=NULL,.func=%s}",key_config_list[i].next,(char*)key_config_list[i].func);
        }
        if(i<list_size-1){
            fprintf(fp,",\n");
        }else{
            fprintf(fp,"\n");
        }
    }
    fprintf(fp,"};\n\n");

    fclose(fp);
    return 0;
}

int register_builtin(const char *name){
    unsigned hash=hash_function(name);
    if(builtin_cmd_hash[hash].key){
        printf("repeat: %d %s %s\n",hash,builtin_cmd_hash[hash].key,name);
        int now=hash;
        while(builtin_cmd_hash[now].next){
            now=builtin_cmd_hash[now].next;
        }
        builtin_cmd_hash[now].next=extra_builtin_real;
        builtin_cmd[extra_builtin_real].key=name;
        builtin_cmd[extra_builtin_real].f=NULL;
        builtin_cmd[extra_builtin_real].next=0;
        extra_builtin_real++;
        return 1;
    }
    builtin_cmd_hash[hash].key=name;
    builtin_cmd_hash[hash].f=NULL;
    builtin_cmd_hash[hash].next=0;
    return 0;
}
