#include <stddef.h>
#include <memory.h>
#include <stdlib.h>
#include "mnsh.h"

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

int da_resize(size_t tp_size,void *array,size_t size){
    darray_t(void) *arr=array;
    void *p=realloc(arr->arr,size);
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
