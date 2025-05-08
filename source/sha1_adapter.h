#ifndef SHA1_ADAPTER_H
#define SHA1_ADAPTER_H

#include <stddef.h> 

#include "sha1.h"
#include "switch/crypto/sha1.h"

typedef struct {
    void (*init)(void* ctx);                 
    void (*update)(void* ctx, const unsigned char* data, size_t len); 
    void (*finalize)(void* ctx, unsigned char* hash); 
} Sha1Adapter;

typedef enum {
    SHA1_REID, 
    SHA1_BUILTIN,
    SHA1_COUNT     //track number of impls
} Sha1ImplementationType;

Sha1Adapter* getSha1Adapter(Sha1ImplementationType impl);

typedef union {
    struct {
        SHA1_CTX ctx; 
    } reid;
    struct {
        Sha1Context ctx;  
    } builtin;

} Sha1ContextUnion;

typedef struct {
    const char* name;
    Sha1ImplementationType type;
} Sha1Mapping;

extern Sha1Mapping shaMappings[];
#endif // SHA1_ADAPTER_H
