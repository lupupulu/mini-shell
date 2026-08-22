#ifndef DARRAY_H
#define DARRAY_H

#if !(defined (__GNUG__)&&defined(size_t))
typedef __SIZE_TYPE__ size_t;
#ifdef __BEOS__
typedef long ssize_t;
#endif
#endif

#define darray_t(Tp) \
struct{\
    Tp *arr;\
    size_t size,real;\
}

int da_init(void *array);
int da_add(size_t tp_size,void *array,const void *buf);
int da_add_loc(size_t tp_size,void *array,const void *buf,size_t i);
int da_del(size_t tp_size,void *array,size_t i);
int da_resize(size_t tp_size,void *array,size_t size);
int da_fake_pop(size_t tp_size,void *array);
int da_pop(size_t tp_size,void *array);
int da_fake_clear(void *array);
int da_clear(void *array);


#ifdef DARRAY_IMPLEMENTATION

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>

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

int da_add_loc(size_t tp_size,void *array,const void *buf,size_t i){
    darray_t(void) *arr=array;
    if(i==arr->size){
        return da_add(tp_size,array,buf);
    }
    da_add(tp_size,array,buf);
    memmove(arr->arr+tp_size*(i+1),arr->arr+tp_size*i,tp_size*(arr->size-i-1));
    memcpy(arr->arr+tp_size*i,buf,tp_size);
    return 0;
}

int da_del(size_t tp_size,void *array,size_t i){
    darray_t(void) *arr=array;
    if(!arr->size){
        return 0;
    }else if(arr->size-1==i){
        return da_pop(tp_size,array);
    }
    memmove(arr->arr+tp_size*i,arr->arr+tp_size*(i+1),tp_size*(arr->size-i-1));
    arr->size--;
    return da_fake_pop(tp_size,array);
}

int da_resize(size_t tp_size,void *array,size_t size){
    darray_t(void) *arr=array;
    void *p=realloc(arr->arr,size*tp_size);
    if(!p){
        return 127;
    }
    arr->arr=p;
    arr->real=size;
    if(arr->real<arr->size){
        arr->size=arr->real;
    }
    return 0;
}

int da_fake_pop(size_t tp_size,void *array){
    darray_t(void) *arr=array;
    if(arr->size<arr->real/2){
        void *p=realloc(arr->arr,arr->real/2*tp_size);
        if(p){
            arr->arr=p;
            arr->real/=2;
        }
    }
    return 0;
}

int da_pop(size_t tp_size,void *array){
    darray_t(void) *arr=array;
    if(!arr->size){
        return 0;
    }
    arr->size--;
    return da_fake_pop(tp_size,array);
}

int da_fake_clear(void *array){
    darray_t(void) *arr=array;
    arr->size=0;
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