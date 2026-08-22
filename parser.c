#include "mnsh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    CMD_RESERVED,
    CMD_EXECUTE,
    CMD_BREAK,

    CMD_PIPE,
    CMD_BACKGROUND,

    CMD_AND,
    CMD_OR,

    CMD_REDIR_IN,
    CMD_REDIR_OUT,
    CMD_REDIR_OUT_ADD,
    CMD_REDIR_DUP,
    CMD_REDIR_HERE_DOCUMENT,
    CMD_REDIR_HERE_STRING,
    CMD_REDIR_CLOSE,

    CMD_IF,
    CMD_THEN,
    CMD_ELSE,
    CMD_FI,

    CMD_CASE,
    CMD_ESAC,
    CMD_CASE_BREAK,

    CMD_FOR,
    CMD_WHILE,
    CMD_UNTIL,
    CMD_DO,
    CMD_DONE,

    CMD_IN,

    CMD_FUNCTION,

    CMD_PART_START,
    CMD_PART_END,
    CMD_SUBSHELL_START,
    CMD_SUBSHELL_END,
    CMD_MATH_START,
    CMD_MATH_END,

    CMD_CREATE_PIPE,
    CMD_DELETE_PIPE,
    CMD_CONTINUE_PIPE,

    CMD_BACKGROUND_START,
    CMD_BACKGROUND_END

}cmd_type_t;


/* NOTE: `loc` MUST stay the first member of both structures:
   cmds->data.arr[i] stores a pointer to the loc field itself, so
   casting that pointer back to type_execute / type_redir relies on
   loc being at offset 0. */
typedef struct{
    size_t loc;          /* index into cmds->type */
    char **argraw;       /* owned: NULL-terminated array of char* (words point into the input buffer) */
}type_execute;

typedef struct{
    size_t loc;          /* index into cmds->type */
    int from_fd;         /* -1 = default (stdin/stdout), otherwise the fd before the operator */
    int to_fd;           /* dup target fd; -1 = none */
    char *to_file;       /* owned: target file name, or the here-document delimiter */
    char *body;          /* owned: collected here-document body, or NULL */
}type_redir;

typedef darray_t(int) da_command;
typedef darray_t(size_t*) da_command_data;

typedef struct {
    da_command type;
    da_command_data data;
}commands_t;

static int parse_error;

/* parse session state (reset by parse_deal_break(CMD_RESERVED,...)) */
static int parse_pipe_is_continue;   /* an open pipeline is waiting for its next member */
static int parse_pipe_need_cmd;      /* a command is required right after '|' */
static int parse_case_pattern;       /* inside case: ')' terminates a pattern */
static int parse_in_case;            /* between CMD_CASE and CMD_ESAC */
static int parse_seen_in;            /* CMD_IN already emitted for this case */
static int parse_expect_in;          /* the next word should be the keyword "in" */
static int parse_func_paren;         /* the next ')' belongs to a function definition */
static int parse_open_ops;           /* number of unclosed { ( } groups in the current segment */



static inline void *parse_create_execute(char **argraw);
static inline void *parse_create_redir(int from_fd);

static inline void parse_add_type(commands_t *cmds,int type,size_t *data);
static inline void parse_add_type_loc(commands_t *cmds,int type,size_t *data,size_t loc);
static inline void parse_add_stype(commands_t *cmds,int type);
static inline void parse_add_stype_loc(commands_t *cmds,int type,size_t loc);

static inline void parse_emit_execute(commands_t *cmds,char **argraw);

static inline void parse_skip_dollar(char *buf,size_t *i);
static inline cmd_type_t parse_is_break(const char *buf,size_t *i);
static inline cmd_type_t parse_is_keyword(const char *buf,size_t *i);
static inline int parse_is_fd(const char *buf);
static inline int strabcmp(const char *a,const char *b);
static inline int parse_is_funcname(const char *s,size_t len);
static inline int parse_is_execute_break(cmd_type_t t);

static inline int parse_set_pipe_start(commands_t *cmds);

static void parse_deal_break(cmd_type_t break_type,commands_t *cmds,char **argraw,int fd_adjacent);
static void parse_attach_redir_target(commands_t *cmds,const char *target,char *buf,size_t *i);
static void parse_collect_heredoc(char *buf,size_t *i,const char *delim,char **body);
static int parse_validate(const commands_t *cmds);

static commands_t *parse_divide_command(char *buf);

/* Free the heap contents of a commands_t produced by parse_divide_command:
   every type_execute / type_redir structure and the buffers they own
   (argraw lists, to_file copies, here-document bodies).
   Safe on partially built structures (the error path uses a stack struct).
   The commands_t object itself is NOT freed — the caller releases it
   (free(cmds)) when it was heap-allocated by parse_divide_command. */
void parse_commands_free(commands_t *cmds){
    if(!cmds){
        return;
    }
    for(size_t i=0;i<cmds->data.size;i++){
        size_t loc=*cmds->data.arr[i];
        if(loc<cmds->type.size&&cmds->type.arr[loc]==CMD_EXECUTE){
            type_execute *e=(void*)cmds->data.arr[i];
            free(e->argraw);
        }else if(loc<cmds->type.size&&cmds->type.arr[loc]>=CMD_REDIR_IN&&cmds->type.arr[loc]<=CMD_REDIR_CLOSE){
            type_redir *r=(void*)cmds->data.arr[i];
            free(r->to_file);
            free(r->body);
        }
        free(cmds->data.arr[i]);
    }
    da_clear(&cmds->type);
    da_clear(&cmds->data);
}


static commands_t *parse_divide_command(char *buf){
    commands_t cmds;
    da_init(&cmds.type);
    da_init(&cmds.data);

    size_t start=0,i=0;
    darray_t(char*) arg;
    da_init(&arg);
    void *cnull=NULL;

    int is_redir=0;         /* a redirector was seen; the next word is its target */
    int quote=0;
    int for_var_pending=0;  /* the next word is the for-loop variable */

    parse_deal_break(CMD_RESERVED,NULL,NULL,0);

    while(buf[i]!='\0'){
        if(parse_error){
            goto L_error;
        }

        size_t next_i=i;
        cmd_type_t break_type=parse_is_break(buf,&next_i);

        /* inside case: ')' terminates the pattern list */
        if(break_type==CMD_SUBSHELL_END&&parse_case_pattern&&!is_redir&&!quote){
            if(i!=start){
                char *part=&buf[start];
                buf[i++]='\0';
                da_add(sizeof(char*),&arg,&part);
            }
            da_add(sizeof(char*),&arg,&cnull);
            if(arg.arr[0]){
                parse_emit_execute(&cmds,arg.arr);
                parse_add_stype(&cmds,CMD_BREAK);
                da_init(&arg);
            }else{
                da_clear(&arg);
            }
            parse_case_pattern=0;
            i=next_i;
            start=i;
            continue;
        }

        /* ')' of a function definition is pure syntax */
        if(break_type==CMD_SUBSHELL_END&&parse_func_paren&&!is_redir&&!quote){
            parse_func_paren=0;
            da_clear(&arg);
            i=next_i;
            start=i;
            continue;
        }

        /* '#' starts a comment only at the start of a word (e.g. not in "$#") */
        if(break_type==CMD_BREAK&&buf[i]=='#'&&i!=start){
            break_type=CMD_RESERVED;
            next_i=i;
        }

        /* ')' '}' are only operators when a matching opener is open */
        if((break_type==CMD_SUBSHELL_END||break_type==CMD_PART_END||break_type==CMD_MATH_END)&&!parse_open_ops){
            break_type=CMD_RESERVED;
            next_i=i;
        }

        /* '(' '{' '((' are only operators at command position, attached to a
           single word (function definition), or right after '=' (array assign) */
        if((break_type==CMD_SUBSHELL_START||break_type==CMD_PART_START||break_type==CMD_MATH_START)
                &&(is_redir||(arg.size&&i==start)||(i>start&&buf[i-1]=='='))){
            break_type=CMD_RESERVED;
            next_i=i;
        }

        /* reserved words only at command position (arg empty, word start) */
        if(!is_redir&&i==start&&arg.size==0&&break_type==CMD_RESERVED){
            break_type=parse_is_keyword(buf,&next_i);
        }else if(!is_redir&&i==start&&arg.size==1&&break_type==CMD_RESERVED&&parse_expect_in){
            /* "in" after the for-variable / case word */
            if(!strabcmp("in",&buf[i])){
                break_type=CMD_IN;
                next_i=i+2;
            }
        }

        /* a newline right after '|' continues the pipeline */
        if(break_type==CMD_BREAK&&buf[i]=='\n'&&parse_pipe_is_continue&&i==start&&!is_redir){
            i=next_i;
            start=i;
            continue;
        }

        /* a newline while waiting for a redirector target continues the line */
        if(break_type==CMD_BREAK&&buf[i]=='\n'&&is_redir&&i==start){
            i=next_i;
            start=i;
            continue;
        }

        /* function definition: name( */
        if(break_type==CMD_SUBSHELL_START&&!is_redir&&!quote&&i!=start&&arg.size==0
                &&parse_is_funcname(&buf[start],i-start)){
            char *part=&buf[start];
            buf[i]='\0';
            i=next_i;
            da_add(sizeof(char*),&arg,&part);
            da_add(sizeof(char*),&arg,&cnull);
            parse_add_stype(&cmds,CMD_FUNCTION);
            parse_emit_execute(&cmds,arg.arr);
            parse_func_paren=1;
            da_init(&arg);
            start=i;
            continue;
        }

        /* word / operator boundary */
        if((!quote&&(buf[i]==' '||buf[i]=='\n'||break_type!=CMD_RESERVED))){
            if(break_type!=CMD_RESERVED&&break_type!=CMD_BREAK&&is_redir){
                fprintf(stderr,"parse: no command before a redirector\n");
                parse_error=1;
                continue;
            }
            if(is_redir){
                /* the current word is the redirector target */
                if(i==start){
                    fprintf(stderr,"parse: missing target for redirector\n");
                    parse_error=1;
                    continue;
                }
                buf[i]='\0';
                size_t tstart=start;
                i++;
                parse_attach_redir_target(&cmds,&buf[tstart],buf,&i);
                is_redir=0;
                start=i;
                continue;
            }
            int fd_adjacent=(i!=start);
            if(i!=start){
                char *part=&buf[start];
                buf[i++]='\0';
                da_add(sizeof(char*),&arg,&part);
                if(for_var_pending){
                    for_var_pending=0;
                    parse_expect_in=1;
                }else if(parse_in_case&&!parse_seen_in){
                    parse_expect_in=1;
                }
            }
            if(break_type!=CMD_RESERVED){
                if(arg.size){
                    da_add(sizeof(char*),&arg,&cnull);
                }
                parse_deal_break(break_type,&cmds,arg.size?arg.arr:NULL,fd_adjacent);
                if(parse_is_execute_break(break_type)){
                    da_init(&arg);
                }else{
                    da_clear(&arg);
                }
                if(break_type==CMD_FOR){
                    for_var_pending=1;
                }
                i=next_i;
            }
            if(break_type>=CMD_REDIR_IN&&break_type<=CMD_REDIR_HERE_STRING){
                is_redir=1;
            }
            break_type=CMD_RESERVED;

            if(buf[i]=='\0'){
                break;
            }

            while(buf[i]==' '||buf[i]=='\n'){
                i++;
            }
            start=i;
            continue;
        }

        switch(buf[i]){
        case '\\':
            i++;
            if(buf[i]!='\0'){
                i++;
            }
            break;

        case '\'':
            i++;
            while(buf[i]&&buf[i]!='\''){
                i++;
            }
            if(buf[i]=='\0'){
                fprintf(stderr,"parse: unterminated single quote\n");
                parse_error=1;
            }else{
                i++;
            }
            break;

        case '`':
            i++;
            while(buf[i]&&buf[i]!='`'){
                if(buf[i]=='\\'){
                    i++;
                }
                if(buf[i]=='\0'){
                    parse_error=1;
                    break;
                }
                i++;
            }
            if(buf[i]=='`'){
                i++;
            }
            break;

        case '\"':
            i++;
            quote=!quote;
            break;

        case '$':
            i++;
            parse_skip_dollar(buf,&i);
            break;

        default:
            i++;
            break;
        }
    }

    /* ---- end of input: flush everything pending ---- */
    if(parse_error){
        goto L_error;
    }

    if(is_redir){
        if(i!=start){
            parse_attach_redir_target(&cmds,&buf[start],buf,&i);
        }else{
            fprintf(stderr,"parse: missing target for redirector\n");
            parse_error=1;
        }
        if(parse_error){
            goto L_error;
        }
        is_redir=0;
    }

    if(arg.size){
        da_add(sizeof(char*),&arg,&cnull);
        parse_deal_break(CMD_BREAK,&cmds,arg.arr,0);
    }else if(parse_pipe_need_cmd){
        fprintf(stderr,"parse: expected a command after '|'\n");
        parse_error=1;
        goto L_error;
    }else{
        /* closes an open pipeline */
        parse_deal_break(CMD_BREAK,&cmds,NULL,0);
    }

    if(parse_validate(&cmds)){
        fprintf(stderr,"parse: syntax error: unbalanced or mismatched keywords\n");
        parse_error=1;
        goto L_error;
    }

    commands_t *ret=malloc(sizeof(commands_t));
    memcpy(ret,&cmds,sizeof(commands_t));
    return ret;

L_error:
    parse_commands_free(&cmds);
    da_clear(&arg);
    parse_deal_break(CMD_RESERVED,NULL,NULL,0);
    parse_error=0;
    return NULL;
}

/* kinds of breaks that attach the pending word list to a new CMD_EXECUTE
   (the arg buffer then belongs to that EXECUTE and must not be freed here) */
static inline int parse_is_execute_break(cmd_type_t t){
    switch(t){
    case CMD_BREAK:
    case CMD_AND:
    case CMD_OR:
    case CMD_PIPE:
    case CMD_BACKGROUND:
    case CMD_CASE_BREAK:
    case CMD_IN:
    case CMD_ESAC:
    case CMD_PART_END:
    case CMD_SUBSHELL_END:
    case CMD_MATH_END:
        return 1;
    default:
        return t>=CMD_REDIR_IN&&t<=CMD_REDIR_CLOSE;
    }
}

static void parse_deal_break(cmd_type_t break_type,commands_t *cmds,char **argraw,int fd_adjacent){
    if(break_type==CMD_RESERVED){
        parse_pipe_is_continue=0;
        parse_pipe_need_cmd=0;
        parse_case_pattern=0;
        parse_in_case=0;
        parse_seen_in=0;
        parse_expect_in=0;
        parse_func_paren=0;
        parse_open_ops=0;
        return;
    }

    /* ---- redirectors: strip the adjacent fd word, emit the pending command,
           then insert the redir opcode before the trailing EXECUTE run ---- */
    if(break_type>=CMD_REDIR_IN&&break_type<=CMD_REDIR_CLOSE){
        int fd=-1;
        if(fd_adjacent&&argraw&&argraw[0]){
            size_t n=0;
            while(argraw[n+1]){
                n++;
            }
            int f=parse_is_fd(argraw[n]);
            if(f>=0){
                fd=f;
                argraw[n]=NULL;   /* the fd word is consumed, not an argument */
            }
        }
        if(argraw&&argraw[0]){
            parse_emit_execute(cmds,argraw);
        }else if(argraw){
            /* the pending arg list became empty (fd word consumed): release it */
            free(argraw);
            argraw=NULL;
        }
        /* insert the redir right before its own command: the LAST EXECUTE of
           the trailing run (or at the end when there is no trailing run) */
        size_t loc=cmds->type.size;
        while(loc>0&&cmds->type.arr[loc-1]==CMD_EXECUTE){
            loc--;
        }
        if(loc<cmds->type.size){
            loc=cmds->type.size-1;
        }
        parse_add_type_loc(cmds,break_type,parse_create_redir(fd),loc);
        return;
    }

    switch(break_type){
    case CMD_CASE_BREAK:
        parse_emit_execute(cmds,argraw);
        parse_add_stype(cmds,CMD_BREAK);
        parse_add_stype(cmds,CMD_BREAK);
        parse_case_pattern=1;     /* the next ')' starts a new pattern */
        break;

    case CMD_BREAK:
        if(argraw&&argraw[0]){
            parse_emit_execute(cmds,argraw);
            parse_add_stype(cmds,CMD_BREAK);
        }
        break;

    case CMD_AND:
    case CMD_OR:
        if(argraw&&argraw[0]){
            parse_emit_execute(cmds,argraw);
        }
        parse_add_stype(cmds,break_type);
        break;

    case CMD_PIPE:
        if(!argraw||!argraw[0]){
            fprintf(stderr,"parse: syntax error near '|'\n");
            parse_error=1;
            break;
        }
        parse_add_stype(cmds,parse_pipe_is_continue?CMD_CONTINUE_PIPE:CMD_CREATE_PIPE);
        parse_pipe_is_continue=1;
        parse_emit_execute(cmds,argraw);
        parse_pipe_need_cmd=1;   /* the member after '|' is still required */
        break;

    case CMD_BACKGROUND:
        if(!argraw||!argraw[0]){
            fprintf(stderr,"parse: syntax error near '&'\n");
            parse_error=1;
            break;
        }
        parse_emit_execute(cmds,argraw);
        parse_set_pipe_start(cmds);
        parse_add_stype(cmds,CMD_BACKGROUND_END);
        parse_add_stype(cmds,CMD_BREAK);
        break;

    case CMD_IN:
        if(argraw&&argraw[0]){
            parse_emit_execute(cmds,argraw);
        }
        parse_add_stype(cmds,CMD_IN);
        if(parse_in_case){
            parse_seen_in=1;
            parse_case_pattern=1;
        }
        break;

    case CMD_CASE:
        parse_add_stype(cmds,CMD_CASE);
        parse_in_case=1;
        break;

    case CMD_ESAC:
        if(argraw&&argraw[0]){
            parse_emit_execute(cmds,argraw);
        }
        parse_add_stype(cmds,CMD_ESAC);
        parse_in_case=0;
        parse_seen_in=0;
        parse_case_pattern=0;
        parse_expect_in=0;
        break;

    case CMD_DO:
        parse_add_stype(cmds,CMD_DO);
        parse_expect_in=0;
        break;

    case CMD_IF:
    case CMD_THEN:
    case CMD_ELSE:
    case CMD_FI:
    case CMD_FOR:
    case CMD_WHILE:
    case CMD_UNTIL:
    case CMD_DONE:
        parse_add_stype(cmds,break_type);
        break;

    case CMD_SUBSHELL_START:
    case CMD_MATH_START:
    case CMD_PART_START:
        parse_add_stype(cmds,break_type);
        parse_open_ops++;
        break;

    case CMD_SUBSHELL_END:
    case CMD_MATH_END:
    case CMD_PART_END:
        if(argraw&&argraw[0]){
            parse_emit_execute(cmds,argraw);
        }
        parse_add_stype(cmds,break_type);
        if(parse_open_ops){
            parse_open_ops--;
        }
        break;

    default:
        break;
    }

    /* an open pipeline ends when a non-pipe separator arrives */
    if(break_type!=CMD_PIPE&&parse_pipe_is_continue){
        parse_pipe_is_continue=0;
        parse_add_stype(cmds,CMD_DELETE_PIPE);
        parse_add_stype(cmds,CMD_BREAK);
    }
}

static void parse_attach_redir_target(commands_t *cmds,const char *target,char *buf,size_t *i){
    /* find the last redirector that still waits for its target */
    size_t idx=cmds->data.size;
    cmd_type_t t=CMD_RESERVED;
    while(idx>0){
        idx--;
        size_t loc=*cmds->data.arr[idx];
        if(loc>=cmds->type.size){
            continue;
        }
        t=cmds->type.arr[loc];
        if(t>=CMD_REDIR_IN&&t<=CMD_REDIR_CLOSE){
            break;
        }
        t=CMD_RESERVED;
    }
    if(t<CMD_REDIR_IN||t>CMD_REDIR_CLOSE){
        fprintf(stderr,"parse: internal error: no redirector awaiting a target\n");
        parse_error=1;
        return;
    }

    type_redir *r=(void*)cmds->data.arr[idx];
    if(t==CMD_REDIR_DUP){
        int fd=parse_is_fd(target);
        if(fd<0){
            fprintf(stderr,"parse: invalid fd after '>&' or '<&': '%s'\n",target);
            parse_error=1;
            return;
        }
        r->to_fd=fd;
        r->to_file=NULL;
    }else{
        r->to_file=strdup(target);
        if(t==CMD_REDIR_HERE_DOCUMENT){
            parse_collect_heredoc(buf,i,r->to_file,&r->body);
        }
    }
}

/* collect the here-document body: lines after the <<delimiter line,
   up to (and excluding) a line equal to delim */
static void parse_collect_heredoc(char *buf,size_t *i,const char *delim,char **body){
    /* compare against the delimiter with surrounding quotes stripped */
    size_t dlen=strlen(delim);
    char *cmp=malloc(dlen+1);
    size_t c=0;
    for(size_t k=0;k<dlen;k++){
        if(delim[k]=='\''||delim[k]=='\"'){
            continue;
        }
        cmp[c++]=delim[k];
    }
    cmp[c]='\0';

    da_str b;
    da_init(&b);

    int found=0;
    while(buf[*i]){
        size_t line=*i;
        while(buf[*i]&&buf[*i]!='\n'){
            (*i)++;
        }
        size_t llen=*i-line;
        if(llen==strlen(cmp)&&!memcmp(buf+line,cmp,llen)){
            found=1;   /* delimiter line */
            break;
        }
        for(size_t k=0;k<llen;k++){
            da_add(sizeof(char),&b,&buf[line+k]);
        }
        da_add(sizeof(char),&b,"\n");
        if(buf[*i]=='\n'){
            (*i)++;
        }
    }
    if(buf[*i]=='\n'){
        (*i)++;
    }
    if(!found){
        fprintf(stderr,"parse: here-document delimited by end-of-file (wanted '%s')\n",cmp);
        free(b.arr);
        free(cmp);
        parse_error=1;
        return;
    }
    da_add(sizeof(char),&b,"");
    *body=b.arr;
    free(cmp);
}

/* verify that keywords form a well-nested structure:
   if..then..fi / for|while|until..do..done / case..esac / {..} (..) ((..)) */
static int parse_validate(const commands_t *cmds){
    int stack[64];
    size_t sp=0;
    for(size_t i=0;i<cmds->type.size;i++){
        int t=cmds->type.arr[i];
        switch(t){
        case CMD_IF:
        case CMD_FOR:
        case CMD_WHILE:
        case CMD_UNTIL:
        case CMD_CASE:
        case CMD_PART_START:
        case CMD_SUBSHELL_START:
        case CMD_MATH_START:
            if(sp>=64){
                return -1;
            }
            stack[sp++]=t;
            break;
        case CMD_THEN:
            if(!sp||stack[sp-1]!=CMD_IF){
                return -1;
            }
            stack[sp-1]=CMD_THEN;
            break;
        case CMD_FI:
            if(!sp||stack[sp-1]!=CMD_THEN){
                return -1;
            }
            sp--;
            break;
        case CMD_DO:
            if(!sp||(stack[sp-1]!=CMD_FOR&&stack[sp-1]!=CMD_WHILE&&stack[sp-1]!=CMD_UNTIL)){
                return -1;
            }
            stack[sp-1]=CMD_DONE;
            break;
        case CMD_DONE:
            if(!sp||stack[sp-1]!=CMD_DONE){
                return -1;
            }
            sp--;
            break;
        case CMD_ESAC:
            if(!sp||stack[sp-1]!=CMD_CASE){
                return -1;
            }
            sp--;
            break;
        case CMD_PART_END:
            if(!sp||stack[sp-1]!=CMD_PART_START){
                return -1;
            }
            sp--;
            break;
        case CMD_SUBSHELL_END:
            if(!sp||stack[sp-1]!=CMD_SUBSHELL_START){
                return -1;
            }
            sp--;
            break;
        case CMD_MATH_END:
            if(!sp||stack[sp-1]!=CMD_MATH_START){
                return -1;
            }
            sp--;
            break;
        default:
            break;
        }
    }
    return sp==0?0:-1;
}

static inline void parse_skip_dollar(char *buf,size_t *i){
#define i (*i)
    switch(buf[i]){
    case '\'':
        i++;
        while(buf[i]&&buf[i]!='\''){
            i++;
        }
        if(buf[i]){
            i++;
        }
        break;

    case '{':
        /* ${...} with nested ${...} support */
        {
            int depth=1;
            i++;
            while(buf[i]&&depth){
                if(buf[i]=='{'){
                    depth++;
                }else if(buf[i]=='}'){
                    depth--;
                }
                i++;
            }
        }
        break;

    case '(':
        if(buf[i+1]=='('){
            /* $(( arithmetic )) — skip to the matching "))" */
            int depth=1;
            i+=2;
            while(buf[i]&&depth){
                if(buf[i]=='('&&buf[i+1]=='('){
                    depth++;
                    i+=2;
                    continue;
                }
                if(buf[i]==')'&&buf[i+1]==')'){
                    depth--;
                    i+=2;
                    continue;
                }
                if(buf[i]=='\\'){
                    i++;
                    if(buf[i]){
                        i++;
                    }
                    continue;
                }
                if(buf[i]=='\''){
                    i++;
                    while(buf[i]&&buf[i]!='\''){
                        i++;
                    }
                    if(buf[i]){
                        i++;
                    }
                    continue;
                }
                if(buf[i]=='\"'){
                    i++;
                    while(buf[i]&&buf[i]!='\"'){
                        i++;
                    }
                    if(buf[i]){
                        i++;
                    }
                    continue;
                }
                i++;
            }
        }else{
            /* $(...) with nesting support */
            int depth=1;
            i++;
            while(buf[i]&&depth){
                switch(buf[i]){
                case '\\':
                    i++;
                    if(buf[i]!='\0'){
                        i++;
                    }
                    break;
                case '\'':
                    i++;
                    while(buf[i]&&buf[i]!='\''){
                        i++;
                    }
                    if(buf[i]){
                        i++;
                    }
                    break;
                case '\"':
                    i++;
                    while(buf[i]&&buf[i]!='\"'){
                        i++;
                    }
                    if(buf[i]){
                        i++;
                    }
                    break;
                case '(':
                    depth++;
                    i++;
                    break;
                case ')':
                    depth--;
                    i++;
                    break;
                case '$':
                    i++;
                    parse_skip_dollar(buf,&i);
                    break;
                default:
                    i++;
                    break;
                }
            }
        }
        break;
    }
#undef i
}

static inline int stracmp(const char *a,const char *b){
    int i=0;
    while(a[i]&&b[i]&&a[i]==b[i]){
        i++;
    }
    if(a[i]=='\0'){
        return 0;
    }
    return a[i]>b[i]?i+1:-i-1;
}
static inline int strabcmp(const char *a,const char *b){
    int i=0;
    while(a[i]&&b[i]&&a[i]==b[i]){
        i++;
    }
    if(a[i]=='\0'&&(b[i]==' '||b[i]=='\n'||b[i]=='\0')){
        return 0;
    }
    return a[i]>b[i]?i+1:-i-1;
}
static inline cmd_type_t parse_is_break(const char *buf,size_t *i){
    const struct{
        char key[4];
        int value;
    }symbol[]={
        {.key="&&" ,.value=CMD_AND                },
        {.key="||" ,.value=CMD_OR                 },
        {.key=";;" ,.value=CMD_CASE_BREAK         },
        {.key="|"  ,.value=CMD_PIPE               },
        {.key="&"  ,.value=CMD_BACKGROUND         },
        {.key=";"  ,.value=CMD_BREAK              },
        {.key="<<<",.value=CMD_REDIR_HERE_STRING  },
        {.key="<&-",.value=CMD_REDIR_CLOSE        },
        {.key="<<" ,.value=CMD_REDIR_HERE_DOCUMENT},
        {.key="<&" ,.value=CMD_REDIR_DUP          },
        {.key="<"  ,.value=CMD_REDIR_IN           },
        {.key=">&-",.value=CMD_REDIR_CLOSE        },
        {.key=">>" ,.value=CMD_REDIR_OUT_ADD      },
        {.key=">&" ,.value=CMD_REDIR_DUP          },
        {.key=">"  ,.value=CMD_REDIR_OUT          },
        {.key="{"  ,.value=CMD_PART_START         },
        {.key="}"  ,.value=CMD_PART_END           },
        {.key="((" ,.value=CMD_MATH_START         },
        {.key="))" ,.value=CMD_MATH_END           },
        {.key="("  ,.value=CMD_SUBSHELL_START     },
        {.key=")"  ,.value=CMD_SUBSHELL_END       }
    };

    if(buf[*i]=='\n'||buf[*i]=='\0'){
        while(buf[(*i)]==' '||buf[(*i)]=='\n'){
            (*i)++;
        }
        return CMD_BREAK;
    }
    if(buf[*i]=='#'){
        while(buf[(*i)]&&buf[(*i)]!='\n'){
            (*i)++;
        }
        return CMD_BREAK;
    }

    for(size_t j=0;j<sizeof(symbol)/sizeof(symbol[0]);j++){
        if(!stracmp(symbol[j].key,&buf[(*i)])){
            (*i)+=strlen(symbol[j].key);
            return symbol[j].value;
        }
    }
    return CMD_RESERVED;
}
static inline cmd_type_t parse_is_keyword(const char *buf,size_t *i){
    const struct{
        char key[8];
        int value;
    }keyword[]={
        {.key="if"   ,.value=CMD_IF   },
        {.key="then" ,.value=CMD_THEN },
        {.key="else" ,.value=CMD_ELSE },
        {.key="fi"   ,.value=CMD_FI   },
        {.key="case" ,.value=CMD_CASE },
        {.key="esac" ,.value=CMD_ESAC },
        {.key="for"  ,.value=CMD_FOR  },
        {.key="while",.value=CMD_WHILE},
        {.key="until",.value=CMD_UNTIL},
        {.key="do"   ,.value=CMD_DO   },
        {.key="done" ,.value=CMD_DONE },
        {.key="in"   ,.value=CMD_IN   }
    };

    for(size_t j=0;j<sizeof(keyword)/sizeof(keyword[0]);j++){
        if(!strabcmp(keyword[j].key,&buf[(*i)])){
            (*i)+=strlen(keyword[j].key);
            return keyword[j].value;
        }
    }
    return CMD_RESERVED;
}

static inline int parse_is_fd(const char *buf){
    if(buf==NULL){
        return -1;
    }
    size_t i=0;
    int fd=0;
    while(buf[i]){
        if(buf[i]<'0'||buf[i]>'9'){
            return -1;
        }
        if(fd>(0x7fffffff-(buf[i]-'0'))/10){
            return -1;   /* overflow guard */
        }
        fd=fd*10+(buf[i]-'0');
        i++;
    }
    if(fd<0){
        fd=-1;
    }
    return fd;
}

static inline int parse_is_funcname(const char *s,size_t len){
    if(!len){
        return 0;
    }
    if(s[0]>='0'&&s[0]<='9'){
        return 0;
    }
    for(size_t i=0;i<len;i++){
        if(!IS_LEGAL(s[i])){
            return 0;
        }
    }
    return 1;
}


static inline void *parse_create_execute(char **argraw){
    type_execute *r=malloc(sizeof(type_execute));
    *r=(type_execute){.loc=0,.argraw=argraw};
    return r;
}
static inline void *parse_create_redir(int from_fd){
    type_redir *r=malloc(sizeof(type_redir));
    *r=(type_redir){.loc=0,.from_fd=from_fd,.to_fd=-1,.to_file=NULL,.body=NULL};
    return r;
}

static inline void parse_add_type(commands_t *cmds,int type,size_t *data){
    da_add(sizeof(int),&cmds->type,&type);

    *data=cmds->type.size-1;
    da_add(sizeof(size_t*),&cmds->data,&data);
}
static inline void parse_add_type_loc(commands_t *cmds,int type,size_t *data,size_t loc){
    da_add_loc(sizeof(int),&cmds->type,&type,loc);

    *data=loc;
    int inserted=0;
    for(size_t i=0;i<cmds->data.size;i++){
        if(*cmds->data.arr[i]<loc){
            continue;
        }
        if(inserted){
            (*cmds->data.arr[i])++;
        }else{
            da_add_loc(sizeof(size_t*),&cmds->data,&data,i);
            inserted=1;
        }
    }
    if(!inserted){
        da_add(sizeof(size_t *),&cmds->data,&data);
    }
}
static inline void parse_add_stype(commands_t *cmds,int type){
    da_add(sizeof(int),&cmds->type,&type);
}
static inline void parse_add_stype_loc(commands_t *cmds,int type,size_t loc){
    da_add_loc(sizeof(int),&cmds->type,&type,loc);
    /* every stored loc >= the insertion point must move up by one */
    for(size_t i=0;i<cmds->data.size;i++){
        if(*cmds->data.arr[i]>=loc){
            (*cmds->data.arr[i])++;
        }
    }
}

static inline void parse_emit_execute(commands_t *cmds,char **argraw){
    parse_pipe_need_cmd=0;
    parse_add_type(cmds,CMD_EXECUTE,parse_create_execute(argraw));
}


static inline int parse_set_pipe_start(commands_t *cmds){
    size_t i=cmds->type.size-1;
    while(i>0&&cmds->type.arr[i]!=CMD_BREAK){
        i--;
    }
    parse_add_stype_loc(cmds,CMD_BACKGROUND_START,i);
    return 0;
}



int parse_check(const char *buf){
    int quote=0,subparse=0;
    int sqr_bracket=0;
    size_t i=0;
    for(;buf[i]!='\0';i++){
        switch(buf[i]){
        case '\"':
            quote=!quote;
            break;
        case '`':
            subparse=!subparse;
            break;
        case '\'':
            if(quote){
                break;
            }
            i++;
            for(;buf[i]!='\0';i++){
                if(buf[i]=='\''){
                    goto L1;
                }
            }
            return 1;
        case '[':
            sqr_bracket++;
            break;
        case ']':
            sqr_bracket--;
            break;
        case '\\':
            i++;
            break;
        case '#':
            goto L2;
        }
        L1:;
    }
    if(i&&buf[i-1]=='\\'){
        return 1;
    }
    L2:
    if(sqr_bracket<0){
        return -1;
    }
    if(sqr_bracket>0||quote||subparse){
        return 1;
    }
    return 0;
}
