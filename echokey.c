#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int set_terminal_echo(int enable);

const char map[]={
    '0','1','2','3','4','5','6','7','8','9',
    'A','B','C','D','E','F'
};

int num=0;

int main(int argc,char const *argv[]){
    if(argc>1){
        if(strcmp(argv[1],"-n")==0){
            num=1;
        }
    }
    set_terminal_echo(0);
    unsigned char c=0;
    char o[3]={' ',' ',' '};
    while(c!=0x04&&c!=EOF){
        read(STDIN_FILENO,&c,1);
        if(c<=0x20||c==127||num){
            o[1]=map[c%16];
            o[0]=map[c/16];
            write(STDOUT_FILENO,o,3);
        }else{
            write(STDOUT_FILENO,&c,1);
            write(STDOUT_FILENO," ",1);
        }
        if(c=='\n'){
            write(STDOUT_FILENO,"\n",1);
        }
    }
    write(STDOUT_FILENO,"\n",1);
    set_terminal_echo(1);
    return 0;
}

int set_terminal_echo(int enable){
    static struct termios original_termios;
    static int is_saved=0;
    struct termios new_termios;

    if(!is_saved){
        if(tcgetattr(STDIN_FILENO,&original_termios)==-1){
            return 1;
        }
        is_saved=1;
    }

    new_termios=original_termios;

    if(enable){
        new_termios.c_lflag|=(ECHO|ICANON|ECHOE|ECHOK|ECHONL);
    }else{
        new_termios.c_lflag&=~(ECHO|ICANON|ECHOE|ECHOK|ECHONL);
        new_termios.c_cc[VMIN]=1;
        new_termios.c_cc[VTIME]=0;
    }

    if(tcsetattr(STDIN_FILENO,TCSANOW,&new_termios)==-1){
        return 1;
    }

    return 0;
}