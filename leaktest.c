/* leaktest.c - memory ownership verification for the parser.
 * Parses a representative script many times. In FREE mode every result is
 * released through parse_commands_free() and LeakSanitizer must stay quiet.
 * In NOFREE mode (control) the same loop must leak, proving that ownership
 * really lives in the parser's heap objects.
 */
#include "CDS/darray.c"
#include "parser.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *script=
    "if ls; then ls > /tmp/out 2>&1; fi\n"
    "for i in 1 2 3; do echo $i; done\n"
    "case x in a) echo a;; b) echo b;; esac\n"
    "foo() { echo hi; }\n"
    "cat <<EOF\n"
    "hello\nthere\n"
    "EOF\n"
    "a | b | c\n"
    "sleep 1 & echo done\n"
    "( echo sub )\n"
    "echo $((1+2)) ${x:-y}\n"
    "echo hi > /tmp/a 2> /tmp/b\n";

int main(int argc,char **argv){
    int nofree=argc>1&&!strcmp(argv[1],"nofree");
    for(int round=0;round<2000;round++){
        char *buf=malloc(strlen(script)+1);
        strcpy(buf,script);
        commands_t *cmd=parse_divide_command(buf);
        if(!cmd){
            fprintf(stderr,"round %d: parse error\n",round);
            return 1;
        }
        if(!nofree){
            parse_commands_free(cmd);
            free(cmd);
        }
        free(buf);
    }
    printf("leaktest done (%s)\n",nofree?"nofree":"free");
    return 0;
}
