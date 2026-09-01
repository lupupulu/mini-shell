/* ptest.c - parser test driver.
 * Each case runs in a forked child with a hard timeout (catches infinite loops),
 * prints the produced opcode stream, then exits so ASan/LSan can report
 * OOB reads / leaks per case.
 */
#include "CDS/darray.c"
#include "parser.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

static const char *tname(int t){
    static const char *n[]={
        "RESERVED","EXECUTE","BREAK","PIPE","BACKGROUND","AND","OR",
        "REDIR_IN","REDIR_OUT","REDIR_OUT_ADD","REDIR_DUP","REDIR_HERE_DOCUMENT",
        "REDIR_HERE_STRING","REDIR_CLOSE","IF","THEN","ELSE","FI","CASE","ESAC",
        "CASE_BREAK","FOR","WHILE","UNTIL","DO","DONE","IN","FUNCTION",
        "PART_START","PART_END","SUBSHELL_START","SUBSHELL_END","MATH_START","MATH_END",
        "CREATE_PIPE","DELETE_PIPE","CONTINUE_PIPE","BACKGROUND_START","BACKGROUND_END"
    };
    return t>=0&&t<39?n[t]:"???";
}

static void run_parse(const char *input){
    char *buf=malloc(strlen(input)+1);
    strcpy(buf,input);
    commands_t *cmd=parse_divide_command(buf);
    if(!cmd){
        printf("  -> parse error\n");
        free(buf);
        return;
    }
    for(size_t i=0;i<cmd->type.size;i++){
        printf("  %3zu %s",i,tname(cmd->type.arr[i]));
        /* find matching data entry */
        for(size_t d=0;d<cmd->data.size;d++){
            if(*cmd->data.arr[d]!=i) continue;
            if(cmd->type.arr[i]==CMD_EXECUTE){
                type_execute *e=(void*)cmd->data.arr[d];
                printf("  [%s]",e->argraw);
            }else if(cmd->type.arr[i]>=CMD_REDIR_IN&&cmd->type.arr[i]<=CMD_REDIR_CLOSE){
                type_redir *r=(void*)cmd->data.arr[d];
                printf("  (fd=%d",r->from_fd);
                if(r->to_fd>=0) printf(" dup=%d",r->to_fd);
                if(r->to_file) printf(" file=%s",r->to_file);
                if(r->body) printf(" body=[%s]",r->body);
                printf(")");
            }
        }
        printf("\n");
    }
    parse_commands_free(cmd);
    free(buf);
}

static void run_case(const char *name,const char *input){
    pid_t pid=fork();
    if(pid==0){
        alarm(3);
        printf("[%s] input: %s\n",name,input);
        run_parse(input);
        fflush(NULL);
        _exit(0);
    }
    int status=0;
    struct timespec ts={0,200*1000*1000};
    int waited=0;
    while(waitpid(pid,&status,WNOHANG)==0){
        nanosleep(&ts,NULL);
        if(++waited>25){
            kill(pid,SIGKILL);
            waitpid(pid,&status,0);
            printf("[%s] *** TIMEOUT / HANG ***\n",name);
            return;
        }
    }
    if(WIFSIGNALED(status)){
        printf("[%s] *** signal %d (%s) ***\n",name,WTERMSIG(status),
               WTERMSIG(status)==SIGSEGV?"SEGV":WTERMSIG(status)==SIGABRT?"ABRT":"");
    }
}

int main(void){
    run_case("empty","");
    run_case("basic","echo hi\n");
    run_case("basic-no-nl","echo hi");
    run_case("cmd-space","echo hi   there\n");
    run_case("redir","ls > /tmp/out\n");
    run_case("redir-no-nl","ls > /tmp/out");
    run_case("redir-fd","ls 2>/dev/null\n");
    run_case("redir-fd-space","echo hi 2> /dev/null\n");
    run_case("redir-dup","echo hi >&2\n");
    run_case("redir-close","echo hi >&-\n");
    run_case("redir-close-in","cat <&-\n");
    run_case("redir-add","echo hi >> /tmp/out\n");
    run_case("redir-in","cat < /tmp/in\n");
    run_case("redir-two","cmd > a 2> b\n");
    run_case("here-string","cat <<<\"hello world\"\n");
    run_case("here-doc","cat <<EOF\nhello\nthere\nEOF\n");
    run_case("pipe","a | b\n");
    run_case("pipe3","a | b | c\n");
    run_case("pipe-nl","a |\nb\n");
    run_case("and-or","a && b || c\n");
    run_case("bg","sleep 1 &\n");
    run_case("bg-two","sleep 1 & echo done\n");
    run_case("bg-two-bg","sleep 1 & echo done &\n");
    run_case("semi","a; b; c\n");
    run_case("if","if ls; then echo ok; fi\n");
    run_case("if-nl","if ls\nthen echo ok\nfi\n");
    run_case("if-unbalanced","if ls\n");
    run_case("for","for i in 1 2 3; do echo $i; done\n");
    run_case("while","while true; do echo x; done\n");
    run_case("case","case x in a) echo a;; b) echo b;; esac\n");
    run_case("func","foo() { echo hi; }\n");
    run_case("subshell","( echo sub )\n");
    run_case("math","echo $((1+2))\n");
    run_case("quote-double","echo \"a b\" c\n");
    run_case("quote-single","echo 'a b' c\n");
    run_case("backtick","echo `date`\n");
    run_case("dollar","echo $HOME $? $#\n");
    run_case("comment","# just a comment\n");
    run_case("comment-trailing","echo hi # comment\n");
    run_case("hash-in-word","echo $# $? $!\n");
    run_case("hash-glued","echo a#b c\n");
    run_case("keyword-in-word","ifconfig\n");
    run_case("case-break","case x in a) echo a;; esac\n");
    /* ---- second batch: next-stage features & edge cases ---- */
    run_case("fd-as-arg","echo 2 > /tmp/f\n");
    run_case("fd-dup-adj","echo hi 2>&1\n");
    run_case("part-group","{ echo hi; echo bye; }\n");
    run_case("part-inline","echo {a,b} c\n");
    run_case("bg-pipe","a | b &\n");
    run_case("heredoc-quoted","cat <<'EOF'\n$x\nEOF\n");
    run_case("heredoc-unterminated","cat <<EOF\nhello\n");
    run_case("nested-dollar","echo ${a:-${b}} ${x}\n");
    run_case("cmd-subst-nested","echo $(echo (x))\n");
    run_case("math-cmd","((1+2))\n");
    run_case("case-unbalanced","case x in a) echo a;;\n");
    run_case("for-no-do","for i in 1 2; echo x\n");
    run_case("empty-pipe","| b\n");
    run_case("dangling-pipe","a |\n");
    run_case("dangling-bg","&\n");
    run_case("array-assign","x=(a b c)\n");
    run_case("func-space","foo () { echo hi; }\n");
    run_case("quote-unterminated","echo 'abc\n");
    run_case("redir-no-target","echo > \n");
    run_case("redir-double","a > > b\n");
    run_case("subshell-nested","( a; ( b ) )\n");
    run_case("pipe-redir","a | b > f\n");
    run_case("pipe-cont-multi","a |\nb | c\n");
    run_case("esc-newline","echo a \\\nb\n");
    return 0;
}
