#include "mnsh.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

codeset_t codeset;
da_str buffer;

key_setting_list_t key_config[CHAR_MAX];
key_setting_list_t key_config_list[256];
unsigned list_size=1;


da_builtincmd builtincmd;
void init(void);


void init(void){
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

    size_t builtincmd_list_size=sizeof(builtincmd_list)/sizeof(const char*);
    for(size_t i=0;i<builtincmd_list_size;i++){
        size_t j=strarr_find_loc(sizeof(builtincmd_t),&builtincmd,builtincmd_list[i],strcmp);
        builtincmd_t tmp={.key=builtincmd_list[i],.f=NULL};
        da_add_loc(sizeof(builtincmd_t),&builtincmd,&tmp,j);
    }

    return ;
}

int main(int argc,const char *argv[]){
    init();

    FILE *fp=fopen("config.c","w");

    fprintf(fp,"#include \"mnsh.h\"\n\n");


    fprintf(fp,"builtincmd_t builtincmd_arr[]={\n");
    for(int i=0;i<builtincmd.size;i++){
        fprintf(fp,"\t{.key=\"%s\",.f=sh_%s}",builtincmd.arr[i].key,builtincmd.arr[i].key);
        if(i<builtincmd.size-1){
            fprintf(fp,",\n");
        }else{
            fprintf(fp,"\n");
        }
    }
    fprintf(fp,"};\n");
    fprintf(fp,"da_builtincmd builtincmd={.arr=builtincmd_arr,.size=%lu,.real=%lu};\n\n",builtincmd.size,builtincmd.size);

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
