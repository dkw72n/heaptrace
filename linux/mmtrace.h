#ifndef LJJ_MMTRACE_H
#define LJJ_MMTRACE_H

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"{
#endif

void* mmtrace_get_client();

void* mmtrace_stack_begin(void* cli);

void mmtrace_stack_frame(void* cli, void* session, const char* frame);

uint64_t mmtrace_stack_commit(void* cli, void* session);

void mmtrace_malloc(void* cli, void* ptr, size_t n, uint64_t stack);

void mmtrace_free(void* cli, void* ptr);

size_t mmtrace_sample_size(void* cli, size_t n);

#ifdef __cplusplus
}
#endif
#endif
