#include "sha1_adapter.h"
#include <string.h>

#include "sha1.h"
#include <switch.h>
#include "switch/crypto/sha1.h"


// steve reid
void sha1ReidInit(void* ctx) {
    SHA1Init(&((Sha1ContextUnion*)ctx)->reid.ctx);
}

void sha1ReidUpdate(void* ctx, const unsigned char* data, size_t len) {
    SHA1Update(&((Sha1ContextUnion*)ctx)->reid.ctx, data, len);
}

void sha1ReidFinalize(void* ctx, unsigned char* hash) {
    SHA1Final(hash, &((Sha1ContextUnion*)ctx)->reid.ctx);
}

// built in
void sha1BuiltInInit(void* ctx) {
    sha1ContextCreate(&((Sha1ContextUnion*)ctx)->builtin.ctx);
}

void sha1BuiltInUpdate(void* ctx, const unsigned char* data, size_t len) {
    sha1ContextUpdate(&((Sha1ContextUnion*)ctx)->builtin.ctx, data, len);
}

void sha1BuiltInFinalize(void* ctx, unsigned char* hash) {
    sha1ContextGetHash(&((Sha1ContextUnion*)ctx)->builtin.ctx, hash);
}
Sha1Adapter sha1ReidAdapter = {
    sha1ReidInit,
    sha1ReidUpdate,
    sha1ReidFinalize
};

Sha1Adapter sha1BuiltInAdapter = {
    sha1BuiltInInit,
    sha1BuiltInUpdate,
    sha1BuiltInFinalize
};

Sha1Adapter* sha1Adapters[SHA1_COUNT] = {
    &sha1ReidAdapter,
    &sha1BuiltInAdapter,
};

Sha1Adapter* getSha1Adapter(Sha1ImplementationType impl) {
    if (impl >= 0 && impl < SHA1_COUNT) {
        return sha1Adapters[impl];
    }
    return NULL;  
}

//used in config
Sha1Mapping shaMappings[] = {
    {"builtin", SHA1_BUILTIN}, //default
    {"reid",    SHA1_REID},
};