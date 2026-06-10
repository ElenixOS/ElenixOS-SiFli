#ifndef MEM_MGR_H
#define MEM_MGR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

void *mem_mgr_alloc(size_t size);
void  mem_mgr_free(void *ptr);
void *mem_mgr_realloc(void *ptr, size_t new_size);
void *mem_mgr_calloc(size_t num, size_t size);

#ifdef __cplusplus
}
#endif

#endif
