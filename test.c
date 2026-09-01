#include "CDS/darray.c"
#include "parser.c"

#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

char *read_file(const char *filename){
    int fd=open(filename,O_RDONLY);
    struct stat status;
    fstat(fd,&status);
    char *buf=malloc(status.st_size+1);
    read(fd,buf,status.st_size);
    buf[status.st_size]='\0';
    close(fd);
    return buf;
}

int main(void){
    char *buf=read_file("test.sh");
    commands_t *cmd=parse_divide_command(buf);
    if(!cmd){
        return 1;
    }
    for(size_t i=0;i<cmd->type.size;i++){
        printf("%lu %d\n",i,cmd->type.arr[i]);
    }
    for(size_t i=0;i<cmd->data.size;i++){
        printf("%lu",*cmd->data.arr[i]);
        if(cmd->type.arr[*cmd->data.arr[i]]==CMD_EXECUTE){
            type_execute *e=(void*)cmd->data.arr[i];
            printf("\t%s\n",e->argraw);
        }else if(cmd->type.arr[*cmd->data.arr[i]]>=CMD_REDIR_IN&&cmd->type.arr[*cmd->data.arr[i]]<=CMD_REDIR_CLOSE){
            type_redir *r=(void*)cmd->data.arr[i];
            printf("\t%d %d\n",r->from_fd,r->to_fd);
            if(r->to_file){
                printf("\t%s\n",r->to_file);
            }
        }
        printf("\n");
    }
    parse_commands_free(cmd);
    free(cmd);
    free(buf);
    return 0;
}