#ifndef CONFC_H
#define CONFC_H

#if !(defined (__GNUG__)&&defined(size_t))
typedef __SIZE_TYPE__ size_t;
#ifdef __BEOS__
typedef long ssize_t;
#endif
#endif

typedef struct {
    char *key;
    char *value;
}ini_pair_t;

typedef struct {
    char *name;
    size_t pair_size;
    ini_pair_t *pairs;
}ini_section_t;

typedef struct {
    size_t section_size;
    ini_section_t *sections;
}ini_t;

ini_t ini_load(const char *filename);
ini_t ini_load_from_memory(const char *file);
void ini_unload(ini_t *file);

ini_section_t *ini_add_section(ini_t *file,ini_section_t *section);
void ini_add_pair(ini_section_t *section,ini_pair_t *pair);

int ini_del_section(ini_t *file,const char *section);
int ini_del_pair(ini_t *file,const char *section,const char *key);
int ini_del_pair_from_section(ini_section_t *section,const char *key);

const char *ini_lookup(ini_t *file,const char *section,const char *key);
ini_section_t *ini_lookup_section(ini_t *file,const char *name);
const char *ini_lookup_from_section(ini_section_t *section,const char *key);


#ifdef CONFC_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>


// darray.h

#ifndef DARRAY_H

#define darray_t(Tp) \
struct{\
    Tp *arr;\
    size_t size,real;\
}

inline static int da_init(void *array);
inline static int da_add(size_t tp_size,void *array,const void *buf);
inline static int da_clear(void *array);

#endif

typedef darray_t(char) da_str;

// confc.c

ini_t ini_load(const char *filename){
    ini_t ret={};
    int fd=open(filename,O_RDONLY);
    if(fd==-1){
        return ret;
    }

    struct stat status;
    if(fstat(fd,&status)!=0){
        return ret;
    }
    char *mem=malloc(status.st_size+1);
    read(fd,mem,status.st_size);
    close(fd);

    mem[status.st_size]='\0';
    ret=ini_load_from_memory(mem);

    free(mem);
    return ret;
}

inline static void ini_init_section(ini_t *file,ini_section_t **now_section){
    char *key=malloc(1);
    key[0]='\0';
    ini_section_t section={.name=key};
    *now_section=ini_add_section(file,&section);
}
static void ini_parse_per_line(ini_t *ret,ini_section_t **now_section,const char *file,size_t *i){
#define i (*i)

    da_str key,value;
    da_init(&key);
    da_init(&value);

    while(file[i]==' '){
        i++;
    }
    if(file[i]=='\n'||file[i]=='\0'){
        return ;
    }

    if(file[i]=='['){
        i++;
        while(file[i]==' '){
            i++;
        }
        while(file[i]!=']'&&file[i]!='\0'){
            da_add(sizeof(char),&key,&file[i]);
            i++;
        }
        if(file[i]=='\0'){
            da_clear(&key);
            return ;
        }
        while(key.size&&key.arr[key.size-1]==' '){
            key.size--;
        }

        da_add(sizeof(char),&key,"");
        ini_section_t section={.name=key.arr};
        *now_section=ini_add_section(ret,&section);
        while(file[i]!='\n'&&file[i]!='\0'){
            i++;
        }
        return ;
    }else if(file[i]=='#'){
        while(file[i]!='\n'&&file[i]!='\0'){
            i++;
        }
        return ;
    }

    while(file[i]!='='&&file[i]!='#'&&file[i]!='\n'&&file[i]!='\0'){
        da_add(sizeof(char),&key,&file[i]);
        i++;
    }
    if(key.size==0){
        da_clear(&key);
        return ;
    }
    while(key.arr[key.size-1]==' '){
        key.size--;
    }
    da_add(sizeof(char),&key,"");

    if(file[i]!='='){
        goto L_ADD_PAIR;
    }

    i++;
    while(file[i]==' '){
        i++;
    }
    while(file[i]!='#'&&file[i]!='\n'&&file[i]!='\0'){
        da_add(sizeof(char),&value,&file[i]);
        i++;
    }
    while(value.size&&value.arr[value.size-1]==' '){
        value.size--;
    }
    L_ADD_PAIR:
    da_add(sizeof(char),&value,"");

    ini_pair_t pair={.key=key.arr,.value=value.arr};
    if(*now_section==NULL){
        ini_init_section(ret,now_section);
    }
    ini_add_pair(*now_section,&pair);

    if(file[i]=='#'){
        while(file[i]!='\n'&&file[i]!='\0'){
            i++;
        }
    }

#undef i
}
ini_t ini_load_from_memory(const char *file){
    ini_t ret={};
    ini_section_t *now_section=NULL;

    size_t i=0;
    while(file[i]!='\0'){
        ini_parse_per_line(&ret,&now_section,file,&i);
        if(file[i]!='\0'){
            i++;
        }
    }

    return ret;
}

void ini_unload(ini_t *file){
    for(size_t i=0;i<file->section_size;i++){
        free(file->sections[i].name);
        for(size_t j=0;j<file->sections[i].pair_size;j++){
            free(file->sections[i].pairs[j].key);
            free(file->sections[i].pairs[j].value);
        }
    }
    free(file->sections);
    file->sections=0;
    file->sections=NULL;
}

ini_section_t *ini_add_section(ini_t *file,ini_section_t *section){
    if(file->section_size==0){
        file->sections=malloc(sizeof(ini_section_t));
        memcpy(&file->sections[0],section,sizeof(ini_section_t));
        file->section_size++;
        return &file->sections[0];
    }

    size_t l=0,r=file->section_size-1;
    size_t mid=l+(r-l)/2;
    while(l<=r&&r!=(size_t)-1){
        mid=l+(r-l)/2;
        int res=strcmp(file->sections[mid].name,section->name);
        if(res==0){
            for(size_t i=0;i<section->pair_size;i++){
                ini_add_pair(&file->sections[mid],&section->pairs[i]);
            }
            free(section->name);
            return &file->sections[mid];
        }else if(res<0){
            l=mid+1;
        }else{
            r=mid-1;
        }
    }
    file->sections=realloc(file->sections,(file->section_size+1)*sizeof(ini_section_t));
    memmove(&file->sections[l+1],&file->sections[l],(file->section_size-l)*sizeof(ini_section_t));
    memcpy(&file->sections[l],section,sizeof(ini_section_t));
    file->section_size++;

    return &file->sections[l];
}

void ini_add_pair(ini_section_t *section,ini_pair_t *pair){
    if(section->pair_size==0){
        section->pairs=malloc(sizeof(ini_pair_t));
        memcpy(&section->pairs[0],pair,sizeof(ini_pair_t));
        section->pair_size++;
        return ;
    }

    size_t l=0,r=section->pair_size-1;
    size_t mid=l+(r-l)/2;
    while(l<=r&&r!=(size_t)-1){
        mid=l+(r-l)/2;
        int res=strcmp(section->pairs[mid].key,pair->key);
        if(res==0){
            free(section->pairs[mid].key);
            free(section->pairs[mid].value);
            memcpy(&section->pairs[mid],pair,sizeof(ini_pair_t));
            return ;
        }else if(res<0){
            l=mid+1;
        }else{
            r=mid-1;
        }
    }
    section->pairs=realloc(section->pairs,(section->pair_size+1)*sizeof(ini_pair_t));
    memmove(&section->pairs[l+1],&section->pairs[l],(section->pair_size-l)*sizeof(ini_pair_t));
    memcpy(&section->pairs[l],pair,sizeof(ini_pair_t));
    section->pair_size++;
}

int ini_del_section(ini_t *file,const char *section){
    if(file->section_size==0){
        return 1;
    }

    size_t l=0,r=file->section_size-1;
    size_t mid=l+(r-l)/2;
    while(l<=r&&r!=(size_t)-1){
        mid=l+(r-l)/2;
        int res=strcmp(file->sections[mid].name,section);
        if(res==0){
            break;
        }else if(res<0){
            l=mid+1;
        }else{
            r=mid-1;
        }
    }
    if(l>r||r==(size_t)-1){
        return 1;
    }

    ini_section_t *now=&file->sections[mid];
    for(size_t i=0;i<now->pair_size;i++){
        free(now->pairs[i].key);
        free(now->pairs[i].value);
    }
    free(now->name);

    memmove(&file->sections[mid],&file->sections[mid+1],(file->section_size-mid-1)*sizeof(ini_section_t));
    file->section_size--;
    file->sections=realloc(file->sections,file->section_size*sizeof(ini_section_t));

    return 0;
}

int ini_del_pair(ini_t *file,const char *section,const char *key){
    ini_section_t *sec=ini_lookup_section(file,section);
    if(sec==NULL){
        return 1;
    }
    return ini_del_pair_from_section(sec,key);
}

int ini_del_pair_from_section(ini_section_t *section,const char *key){
    if(section->pair_size==0){
        return 2;
    }

    size_t l=0,r=section->pair_size-1;
    size_t mid=l+(r-l)/2;
    while(l<=r&&r!=(size_t)-1){
        mid=l+(r-l)/2;
        int res=strcmp(section->pairs[mid].key,key);
        if(res==0){
            break;
        }else if(res<0){
            l=mid+1;
        }else{
            r=mid-1;
        }
    }
    if(l>r||r==(size_t)-1){
        return 2;
    }

    free(section->pairs[mid].key);
    free(section->pairs[mid].value);

    memmove(&section->pairs[mid],&section->pairs[mid+1],(section->pair_size-mid-1)*sizeof(ini_pair_t));
    section->pair_size--;
    section->pairs=realloc(section->pairs,section->pair_size*sizeof(ini_pair_t));

    return 0;
}


const char *ini_lookup(ini_t *file,const char *section,const char *key){
    ini_section_t *sec=ini_lookup_section(file,section);
    if(sec==NULL){
        return NULL;
    }
    return ini_lookup_from_section(sec,key);
}

ini_section_t *ini_lookup_section(ini_t *file,const char *name){
    if(file->section_size==0){
        return NULL;
    }

    size_t l=0,r=file->section_size-1;
    while(l<=r&&r!=(size_t)-1){
        size_t mid=l+(r-l)/2;
        int res=strcmp(file->sections[mid].name,name);
        if(res==0){
            return &file->sections[mid];
        }else if(res<0){
            l=mid+1;
        }else{
            r=mid-1;
        }
    }
    return NULL;
}

const char *ini_lookup_from_section(ini_section_t *section,const char *key){
    if(section->pair_size==0){
        return NULL;
    }

    size_t l=0,r=section->pair_size-1;
    size_t mid=l+(r-l)/2;
    while(l<=r&&r!=(size_t)-1){
        mid=l+(r-l)/2;
        int res=strcmp(section->pairs[mid].key,key);
        if(res==0){
            return section->pairs[mid].value;
        }else if(res<0){
            l=mid+1;
        }else{
            r=mid-1;
        }
    }
    return NULL;
}



// darray.c

#ifndef DARRAY_H

int da_init(void *array){
    memset(array,0,sizeof(darray_t(void)));
    return 0;
}

int da_add(size_t tp_size,void *array,const void *buf){
    darray_t(void) *arr=array;
    if(arr->size+1>=arr->real){
        void *p=realloc(arr->arr,(arr->real*3/2+1)*tp_size);
        if(!p){
            return 127;
        }
        arr->arr=p;
        arr->real=arr->real*3/2+1;
    }
    memcpy(arr->arr+arr->size*tp_size,buf,tp_size);
    arr->size++;
    return 0;
}

int da_clear(void *array){
    darray_t(void) *arr=array;
    free(arr->arr);
    memset(arr,0,sizeof(darray_t(void)));
    return 0;
}

#endif


#endif

#endif