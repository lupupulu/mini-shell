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
    CMD_CASE_BREAK,      /* internal break type only, never emitted */

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
    char *argraw;        /* the complete command, pointing into the input buffer (not owned) */
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

static inline void *parse_create_execute(char *argraw);
static inline void *parse_create_redir(int from_fd);

static inline void parse_add_type(commands_t *cmds,int type,size_t *data);
static inline void parse_add_type_loc(commands_t *cmds,int type,size_t *data,size_t loc);
static inline void parse_add_stype(commands_t *cmds,int type);
static inline void parse_add_stype_loc(commands_t *cmds,int type,size_t loc);

static void parse_emit_execute(commands_t *cmds,char *argraw);
static int parse_emit_command_raw(commands_t *cmds,char *buf,size_t start,size_t end);

static inline void parse_skip_dollar(char *buf,size_t *i);
static inline cmd_type_t parse_is_break(const char *buf,size_t *i);
static inline cmd_type_t parse_is_keyword(const char *buf,size_t *i);
static inline int parse_is_fd(const char *buf);
static inline int parse_is_fd_span(const char *buf,size_t len);
static inline int parse_span_is_blank(const char *buf,size_t start,size_t end);
static inline int stracmp(const char *a,const char *b);
static inline int strabcmp(const char *a,const char *b);
static inline int parse_is_funcname(const char *s,size_t len);

static inline int parse_set_pipe_start(commands_t *cmds);

static void parse_attach_redir_target(commands_t *cmds,const char *target,char *buf,size_t *i);
static void parse_collect_heredoc(char *buf,size_t *i,const char *delim,char **body);
static int parse_validate(const commands_t *cmds);

static commands_t *parse_divide_command(char *buf);

/* Free the heap contents of a commands_t produced by parse_divide_command:
   every type_execute / type_redir structure and the buffers owned by the
   parser (to_file copies, here-document bodies).  type_execute.argraw is
   NOT freed -- it points into the caller's input buffer.
   Safe on partially built structures (the error path uses a stack struct).
   The commands_t object itself is NOT freed -- the caller releases it
   (free(cmds)) when it was heap-allocated by parse_divide_command. */
void parse_commands_free(commands_t *cmds){
    if(!cmds){
        return;
    }
    for(size_t i=0;i<cmds->data.size;i++){
        size_t loc=*cmds->data.arr[i];
        if(loc<cmds->type.size&&cmds->type.arr[loc]>=CMD_REDIR_IN&&cmds->type.arr[loc]<=CMD_REDIR_CLOSE){
            type_redir *r=(void*)cmds->data.arr[i];
            free(r->to_file);
            free(r->body);
        }
        /* type_execute owns nothing on the heap: argraw points into the input */
        free(cmds->data.arr[i]);
    }
    da_clear(&cmds->type);
    da_clear(&cmds->data);
}


static commands_t *parse_divide_command(char *buf){
    commands_t cmds;
    da_init(&cmds.type);
    da_init(&cmds.data);

    size_t start=0,word_start=0,i=0;

    int is_redir=0;         /* a redirector was seen; the next word is its target */
    int quote=0;            /* inside a double-quoted region */

    int pipe_is_continue=0; /* an open pipeline is waiting for its next member */
    int pipe_need_cmd=0;    /* a command is required right after '|' */

    int in_case=0;          /* between CMD_CASE and CMD_ESAC */
    int seen_in=0;          /* CMD_IN already emitted for this case */
    int expect_in=0;        /* the next word should be the keyword "in" */
    int case_pattern=0;     /* inside case: ')' terminates a pattern */
    int for_var_pending=0;  /* the next word is the for-loop variable */

    int func_paren=0;       /* the next ')' belongs to a function definition */
    int open_ops=0;         /* number of unclosed { ( } groups in the current segment */

    /* emit the pending command text [s,e) as one CMD_EXECUTE; when a command
       was actually emitted, the member after '|' has arrived */
#define emit_command(s,e) do{ \
        if(parse_emit_command_raw(&cmds,buf,(s),(e))){ \
            pipe_need_cmd=0; \
        } \
    }while(0)

    while(buf[i]!='\0'){
        if(parse_error){
            goto L_error;
        }

        size_t next_i=i;
        cmd_type_t break_type=parse_is_break(buf,&next_i);

        /* inside case: ')' terminates the pattern list */
        if(break_type==CMD_SUBSHELL_END&&case_pattern&&!is_redir&&!quote){
            if(parse_span_is_blank(buf,start,i)){
                fprintf(stderr,"parse: syntax error: empty case pattern\n");
                parse_error=1;
                goto L_error;
            }
            emit_command(start,i);
            parse_add_stype(&cmds,CMD_BREAK);
            case_pattern=0;

            i=next_i;
            while(buf[i]==' '||buf[i]=='\t'||buf[i]=='\n'){
                i++;
            }
            start=i;
            word_start=i;
            continue;
        }

        /* ')' of a function definition is pure syntax */
        if(break_type==CMD_SUBSHELL_END&&func_paren&&!is_redir&&!quote){
            func_paren=0;
            i=next_i;
            while(buf[i]==' '||buf[i]=='\t'||buf[i]=='\n'){
                i++;
            }
            start=i;
            word_start=i;
            continue;
        }

        /* '#' starts a comment only at the start of a word (e.g. not in "$#") */
        if(break_type==CMD_BREAK&&buf[i]=='#'&&i!=word_start){
            break_type=CMD_RESERVED;
            next_i=i;
        }

        /* ')' '}' are only operators when a matching opener is open */
        if((break_type==CMD_SUBSHELL_END||break_type==CMD_PART_END||break_type==CMD_MATH_END)&&!open_ops){
            break_type=CMD_RESERVED;
            next_i=i;
        }

        /* '(' '{' '((' are only operators at command position, attached to a
           single word (function definition), or right after '=' (array assign) */
        if((break_type==CMD_SUBSHELL_START||break_type==CMD_PART_START||break_type==CMD_MATH_START)
                &&(is_redir||(i==word_start&&!parse_span_is_blank(buf,start,i))||(i>start&&buf[i-1]=='='))){
            break_type=CMD_RESERVED;
            next_i=i;
        }

        /* reserved words only at command position (no pending command text) */
        if(!is_redir&&i==start&&break_type==CMD_RESERVED){
            break_type=parse_is_keyword(buf,&next_i);
        }else if(!is_redir&&i==word_start&&expect_in&&break_type==CMD_RESERVED){
            /* "in" after the for-variable / case word */
            if(!strabcmp("in",&buf[i])){
                break_type=CMD_IN;
                next_i=i+2;
            }
        }

        /* a newline right after '|' continues the pipeline */
        if(break_type==CMD_BREAK&&buf[i]=='\n'&&pipe_is_continue&&i==start&&!is_redir){
            i=next_i;
            start=i;
            word_start=i;
            continue;
        }

        /* a newline while waiting for a redirector target continues the line */
        if(break_type==CMD_BREAK&&buf[i]=='\n'&&is_redir&&i==start){
            i=next_i;
            start=i;
            word_start=i;
            continue;
        }

        /* function definition: name( */
        if(break_type==CMD_SUBSHELL_START&&!is_redir&&!quote&&i!=start&&start==word_start
                &&parse_is_funcname(&buf[start],i-start)){
            buf[i]='\0';
            parse_add_stype(&cmds,CMD_FUNCTION);
            parse_emit_execute(&cmds,&buf[start]);
            func_paren=1;
            i=next_i;
            while(buf[i]==' '||buf[i]=='\t'||buf[i]=='\n'){
                i++;
            }
            start=i;
            word_start=i;
            continue;
        }

        /* ordinary character (or inside quotes): skip it via L */
        if(quote||(buf[i]!=' '&&buf[i]!='\t'&&buf[i]!='\n'&&break_type==CMD_RESERVED)){
            goto L;
        }

        /* ---- word / operator boundary (blank or operator) ---- */

        /* a redirector is waiting for its target: the current word ends here */
        if(is_redir){
            if(break_type!=CMD_RESERVED&&break_type!=CMD_BREAK){
                fprintf(stderr,"parse: syntax error: redirector without a target\n");
                parse_error=1;
                goto L_error;
            }
            if(word_start==i){
                fprintf(stderr,"parse: syntax error: redirector without a target\n");
                parse_error=1;
                goto L_error;
            }
            buf[i]='\0';
            size_t tstart=word_start;
            i++;   /* move past the terminating space / newline */
            parse_attach_redir_target(&cmds,&buf[tstart],buf,&i);
            is_redir=0;
            start=i;
            word_start=i;
            continue;
        }

        /* plain whitespace only: word boundary, no operator */
        if(break_type==CMD_RESERVED){
            goto L_word_boundary;
        }

        /* ---- a redirection operator ---- */
        if(break_type>=CMD_REDIR_IN&&break_type<=CMD_REDIR_CLOSE){
            /* only a digit word directly before the operator is an fd */
            int fd=parse_is_fd_span(&buf[word_start],i-word_start);
            /* an adjacent fd word belongs to the redirector, otherwise
               the whole pending span up to the operator is the command */
            if(fd>=0){
                emit_command(start,word_start);
                goto L_insert_redir;
            }
            emit_command(start,i);

        L_insert_redir:
            /* insert the redir right before its own command: the last
               EXECUTE of the trailing run (or at the end) */
            size_t loc=cmds.type.size;
            while(loc>0&&cmds.type.arr[loc-1]==CMD_EXECUTE){
                loc--;
            }
            if(loc<cmds.type.size){
                loc=cmds.type.size-1;
            }
            parse_add_type_loc(&cmds,break_type,parse_create_redir(fd),loc);
            /* '>&-' / '<&-' close the fd and take no target */
            is_redir=(break_type<=CMD_REDIR_HERE_STRING);
            goto L_after_operator;   /* a redirector takes no separator switch */
        }

        /* ---- a command separator / keyword / grouping operator ---- */
        switch(break_type){
        case CMD_CASE_BREAK:
            emit_command(start,i);
            parse_add_stype(&cmds,CMD_BREAK);
            parse_add_stype(&cmds,CMD_BREAK);
            case_pattern=1;     /* the next ')' starts a new pattern */
            break;

        case CMD_BREAK:
            if(start<i&&!parse_span_is_blank(buf,start,i)){
                emit_command(start,i);
                parse_add_stype(&cmds,CMD_BREAK);
            }
            break;

        case CMD_AND:
        case CMD_OR:
            emit_command(start,i);
            parse_add_stype(&cmds,break_type);
            break;

        case CMD_PIPE:
            if(start==i||parse_span_is_blank(buf,start,i)){
                fprintf(stderr,"parse: syntax error near '|'\n");
                parse_error=1;
                goto L_error;
            }
            parse_add_stype(&cmds,pipe_is_continue?CMD_CONTINUE_PIPE:CMD_CREATE_PIPE);
            pipe_is_continue=1;
            emit_command(start,i);
            pipe_need_cmd=1;   /* the member after '|' is still required */
            break;

        case CMD_BACKGROUND:
            if(start==i||parse_span_is_blank(buf,start,i)){
                fprintf(stderr,"parse: syntax error near '&'\n");
                parse_error=1;
                goto L_error;
            }
            emit_command(start,i);
            parse_set_pipe_start(&cmds);
            parse_add_stype(&cmds,CMD_BACKGROUND_END);
            parse_add_stype(&cmds,CMD_BREAK);
            break;

        case CMD_IN:
            emit_command(start,i);
            parse_add_stype(&cmds,CMD_IN);
            expect_in=0;
            if(in_case){
                seen_in=1;
                case_pattern=1;
            }
            break;

        case CMD_CASE:
            parse_add_stype(&cmds,CMD_CASE);
            in_case=1;
            seen_in=0;
            expect_in=0;
            break;

        case CMD_ESAC:
            emit_command(start,i);
            parse_add_stype(&cmds,CMD_ESAC);
            in_case=0;
            seen_in=0;
            expect_in=0;
            case_pattern=0;
            break;

        case CMD_DO:
            parse_add_stype(&cmds,CMD_DO);
            for_var_pending=0;
            expect_in=0;
            break;

        case CMD_IF:
        case CMD_THEN:
        case CMD_ELSE:
        case CMD_FI:
        case CMD_FOR:
        case CMD_WHILE:
        case CMD_UNTIL:
        case CMD_DONE:
            parse_add_stype(&cmds,break_type);
            if(break_type==CMD_FOR){
                for_var_pending=1;
            }
            break;

        case CMD_SUBSHELL_START:
        case CMD_MATH_START:
        case CMD_PART_START:
            parse_add_stype(&cmds,break_type);
            open_ops++;
            break;

        case CMD_SUBSHELL_END:
        case CMD_MATH_END:
        case CMD_PART_END:
            emit_command(start,i);
            parse_add_stype(&cmds,break_type);
            if(open_ops){
                open_ops--;
            }
            break;

        default:
            fprintf(stderr,"parse: internal error: unhandled operator\n");
            parse_error=1;
            goto L_error;
        }

        /* an open pipeline ends when a non-pipe separator arrives */
        if(break_type!=CMD_PIPE&&pipe_is_continue){
            pipe_is_continue=0;
            parse_add_stype(&cmds,CMD_DELETE_PIPE);
            parse_add_stype(&cmds,CMD_BREAK);
        }

    L_after_operator:
        i=next_i;
        while(buf[i]==' '||buf[i]=='\t'||buf[i]=='\n'){
            i++;
        }
        start=i;
        word_start=i;
        continue;

    L_word_boundary:
        /* ---- plain whitespace: word boundary ---- */
        if(word_start<i){
            if(for_var_pending){
                for_var_pending=0;
                expect_in=1;
            }else if(in_case&&!seen_in){
                expect_in=1;
            }
        }
        i++;
        while(buf[i]==' '||buf[i]=='\t'){
            i++;
        }
        word_start=i;
        /* nothing but blanks pending: still at command position */
        if(parse_span_is_blank(buf,start,i)){
            start=i;
        }
        continue;

        /* ---- ordinary character: skip quoted regions / expansions ---- */
        L:
        switch(buf[i]){
        case '\\':
            i++;
            if(buf[i]!='\0'){
                i++;
            }
            break;

        case '\'':
            if(quote){
                i++;   /* literal quote inside double quotes */
                break;
            }
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
                    fprintf(stderr,"parse: unterminated backquote\n");
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
        if(word_start<i){
            buf[i]='\0';
            parse_attach_redir_target(&cmds,&buf[word_start],buf,&i);
        }else{
            fprintf(stderr,"parse: syntax error: redirector without a target\n");
            parse_error=1;
        }
        if(parse_error){
            goto L_error;
        }
        is_redir=0;
        /* the target word is consumed: nothing stays pending */
        start=i;
        word_start=i;
    }

    if(start<i&&!parse_span_is_blank(buf,start,i)){
        emit_command(start,i);
        parse_add_stype(&cmds,CMD_BREAK);
    }else if(pipe_need_cmd){
        fprintf(stderr,"parse: syntax error: expected a command after '|'\n");
        parse_error=1;
        goto L_error;
    }

    /* closes an open pipeline */
    if(pipe_is_continue){
        pipe_is_continue=0;
        parse_add_stype(&cmds,CMD_DELETE_PIPE);
        parse_add_stype(&cmds,CMD_BREAK);
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
    parse_error=0;
    return NULL;
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
            /* $(( arithmetic )) -- skip to the matching "))" */
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

/* like parse_is_fd, but the digit run is [buf, buf+len) without a
   trailing '\0' (the word before a redirector is not yet terminated) */
static inline int parse_is_fd_span(const char *buf,size_t len){
    if(buf==NULL||len==0){
        return -1;
    }
    int fd=0;
    for(size_t k=0;k<len;k++){
        if(buf[k]<'0'||buf[k]>'9'){
            return -1;
        }
        if(fd>(0x7fffffff-(buf[k]-'0'))/10){
            return -1;   /* overflow guard */
        }
        fd=fd*10+(buf[k]-'0');
    }
    return fd;
}

/* true when [start,end) contains only blanks -- i.e. no command text */
static inline int parse_span_is_blank(const char *buf,size_t start,size_t end){
    for(size_t k=start;k<end;k++){
        if(buf[k]!=' '&&buf[k]!='\t'&&buf[k]!='\n'){
            return 0;
        }
    }
    return 1;
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


static inline void *parse_create_execute(char *argraw){
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

static void parse_emit_execute(commands_t *cmds,char *argraw){
    parse_add_type(cmds,CMD_EXECUTE,parse_create_execute(argraw));
}

/* Emit the pending command text [start,end) as one CMD_EXECUTE whose
   argraw points to buf+start.  Trailing blanks are trimmed so that the
   complete command has no trailing whitespace; buf[end] is overwritten
   with '\0' to terminate the string in place.
   Returns 1 when a command was emitted, 0 when the span was empty. */
static int parse_emit_command_raw(commands_t *cmds,char *buf,size_t start,size_t end){
    while(end>start&&(buf[end-1]==' '||buf[end-1]=='\t'||buf[end-1]=='\n')){
        end--;
    }
    if(end<=start){
        return 0;
    }
    buf[end]='\0';
    parse_emit_execute(cmds,&buf[start]);
    return 1;
}


static inline int parse_set_pipe_start(commands_t *cmds){
    /* insert BACKGROUND_START right after the last CMD_BREAK (or at the
       very beginning), i.e. before the command it wraps */
    size_t i=cmds->type.size;
    while(i>0&&cmds->type.arr[i-1]!=CMD_BREAK){
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
