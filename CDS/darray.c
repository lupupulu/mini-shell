#define DARRAY_IMPLEMENTATION
#include "darray.h"
#include "../mnsh.h"

size_t strarr_find_loc(size_t pair_size,void *array,const char *key,strarr_cmp_func f){
    strarray_t(strpair_t()) *arr=array;
    size_t l=0,r=arr->size,mid=(l+r)/2;
    while(l<r){
        mid=(l+r)/2;
        int cmp=(f?f:strcmp)(*(char**)(((void*)arr->arr)+pair_size*mid),key);
        if(cmp<0){
            l=mid+1;
        }else{
            r=mid;
        }
    }
    return l;
}

size_t strarr_find(size_t pair_size,void *array,const char *key,strarr_cmp_func f){
    size_t loc=strarr_find_loc(pair_size,array,key,f);
    strarray_t(strpair_t()) *arr=array;
    if(loc>=arr->size||(f?f:strcmp)(*(char**)(((void*)arr->arr)+pair_size*loc),key)){
        return STRARR_CANNOT_FIND;
    }
    return loc;
}