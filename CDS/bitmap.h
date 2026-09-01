#ifndef BITMAP_H
#define BITMAP_H

#include <stddef.h>

typedef struct {
    unsigned *data;
    size_t size;
}bitmap_t;

void bm_init(bitmap_t *bitmap,size_t size);
void bm_resize(bitmap_t *bitmap,size_t size);
void bm_clear(bitmap_t *bitmap);
void bm_fake_clear(bitmap_t *bitmap);

int bm_get(bitmap_t *bitmap,size_t i);
void bm_set(bitmap_t *bitmap,size_t i,int v);

#ifdef BITMAP_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

#define NOT_LESS_THAN(size,n) (((size)%(n))?(size)/(n)+1:(size)/(n))

void bm_init(bitmap_t *bitmap,size_t size){
    if(size==0){
        return ;
    }
    bitmap->data=malloc(NOT_LESS_THAN(size,32)*sizeof(unsigned));
    bitmap->size=size;
    memset(bitmap->data,0,NOT_LESS_THAN(size,32)*sizeof(unsigned));
}

void bm_resize(bitmap_t *bitmap,size_t size){
    bitmap->data=realloc(bitmap->data,NOT_LESS_THAN(size,32)*sizeof(unsigned));
    if(size>bitmap->size){
        memset(bitmap->data+NOT_LESS_THAN(bitmap->size,32),0,(NOT_LESS_THAN(size,32)-NOT_LESS_THAN(bitmap->size,32))*sizeof(unsigned));
    }
    bitmap->size=size;
}

void bm_clear(bitmap_t *bitmap){
    free(bitmap->data);
    bitmap->data=NULL;
    bitmap->size=0;
}

void bm_fake_clear(bitmap_t *bitmap){
    bitmap->size=0;
}


int bm_get(bitmap_t *bitmap,size_t i){
    if(i>=bitmap->size){
        return -1;
    }
    return bitmap->data[i/32]&(1u<<(i%32))?1:0;
}
void bm_set(bitmap_t *bitmap,size_t i,int v){
    if(i>=bitmap->size){
        return ;
    }
    unsigned n;
    if(v==0){
        n=1u<<(i%32);
        n=~n;
        bitmap->data[i/32]=bitmap->data[i/32]&n;
    }else{
        n=1u<<(i%32);
        bitmap->data[i/32]=bitmap->data[i/32]|n;
    }
}

#endif

#endif
