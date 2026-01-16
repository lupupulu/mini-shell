#include "mnsh.h"

static inline int utf8_is_cjk(size_t codepoint);
static inline int utf8_is_full_width_char(size_t codepoint);
static inline int utf8_is_korean(size_t codepoint);
static inline int utf8_is_japanese(size_t codepoint);
static inline int utf8_is_wide_emoji(size_t codepoint);

size_t utf8_get_char_width(const char *c){
    if(c[0]>=0){
        return 1;
    }
    size_t codepoint=0;
    if(UTF8_CHECK(c[0],4)){
        codepoint=
        ((c[0]&0x07)<<18)|
        ((c[1]&0x3F)<<12)|
        ((c[2]&0x3F)<<6)|
        ( c[3]&0x3F );
    }else if(UTF8_CHECK(c[0],3)){
        codepoint=
        ((c[0]&0x0F)<<12)|
        ((c[1]&0x3F)<<6)|
        ( c[2]&0x3F );
    }else if(UTF8_CHECK(c[0],2)){
        codepoint=
        ((c[0]&0x1F)<<6)|
        ( c[1]&0x3F );
    }
    
    if(
        utf8_is_cjk(codepoint)||
        utf8_is_full_width_char(codepoint)||
        utf8_is_korean(codepoint)||
        utf8_is_japanese(codepoint)||
        utf8_is_wide_emoji(codepoint)
    ){
        return 2;
    }
    
    return 1;
}

static inline int utf8_is_cjk(size_t codepoint){
    if(
        (codepoint>=0x04E00&&codepoint<=0x09FFF)||
        (codepoint>=0x03400&&codepoint<=0x04DBF)||
        (codepoint>=0x20000&&codepoint<=0x2A6DF)||
        (codepoint>=0x2A700&&codepoint<=0x2B73F)||
        (codepoint>=0x2B740&&codepoint<=0x2B81F)||
        (codepoint>=0x2B820&&codepoint<=0x2CEAF)||
        (codepoint>=0x2CEB0&&codepoint<=0x2EBEF)
    ){
        return 1;
    }
    return 0;
}
static inline int utf8_is_full_width_char(size_t codepoint){
    if(
        (codepoint>=0xFF01&&codepoint<=0xFF60)||
        (codepoint>=0xFFE0&&codepoint<=0xFFE6)
    ){
        return 1;
    }
    return 0;
}
static inline int utf8_is_korean(size_t codepoint){
    if(codepoint>=0xAC00&&codepoint<=0xD7AF){
        return 1;
    }
    return 0;
}
static inline int utf8_is_japanese(size_t codepoint){
    if(
        (codepoint>=0x3040&&codepoint<=0x309F)||
        (codepoint>=0x30A0&&codepoint<=0x30FF)
    ){
        return 1;
    }
    return 0;
}
static inline int utf8_is_wide_emoji(size_t codepoint){
    if(
        (codepoint>=0x1F300&&codepoint<=0x1F5FF)||
        (codepoint>=0x1F900&&codepoint<=0x1F9FF)||
        (codepoint>=0x1F600&&codepoint<=0x1F64F)||
        (codepoint>=0x1F680&&codepoint<=0x1F6FF)||
        (codepoint>=0x02700&&codepoint<=0x027BF)||
        (codepoint>=0x02600&&codepoint<=0x026FF)||
        (codepoint>=0x1F100&&codepoint<=0x1F1FF)||
        (codepoint>=0x1F200&&codepoint<=0x1F2FF)||
        (codepoint>=0x1F800&&codepoint<=0x1F8FF)
    ){
        return 1;
    }
    return 0;
}