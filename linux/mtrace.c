/*
Copyright (c) 2020 dkw72n

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
 * compile:
 *  gcc mtrace.c -std=c11 -ldl -shared -fPIC -g -o mtrace.so -lm
 * 
 * example usage:
 *  LD_PRELOAD=`pwd`/mtrace.so MTRACE_SAMPLE_INTERVAL=2357 MTRACE_OUTPUT_DIR=`pwd` ps -ef
 * */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include "mmtrace.h"

struct r_debug{
  int r_version;
  void* r_map;
  void* r_brk;
};

extern struct r_debug _r_debug;

static void* (*real_malloc)(size_t) = NULL;
static void* (*real_realloc)(void*, size_t) = NULL;
static void* (*real_calloc)(size_t, size_t) = NULL;
static void (*real_free)(void*) = NULL;
static int (*real_posix_memalign)(void **memptr, size_t alignment, size_t size) = NULL;
static void* (*real_memalign)(size_t alignment, size_t size) = NULL;

#define DEFAULT_SAMPLE_INTERVAL 1
static __thread int in_call = 0;
static __thread double next_sample = 0;
static int sample_interval = DEFAULT_SAMPLE_INTERVAL;
static double sample_rate = 1.0/DEFAULT_SAMPLE_INTERVAL;
static FILE* fp = NULL;

double ran_expo(double lambda){
  double u;
  u = rand() / (RAND_MAX + 1.0);
  return -log(1- u) / lambda;
}

size_t sample_size(size_t n){
  if (sample_interval > 1){
    int num_samples = 0;
    next_sample -= n;
    while(next_sample <= 0){
      next_sample += ran_expo(sample_rate);
      num_samples++;
    }
    return num_samples * sample_interval;
  }
  return n;
}

static void mtrace_init(void)
{
  in_call = 1;
  

  real_malloc = dlsym(RTLD_NEXT, "malloc");
  if (NULL == real_malloc) {
    fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
  }
  real_realloc = dlsym(RTLD_NEXT, "realloc");
  if (NULL == real_realloc) {
    fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
  }
  real_calloc = dlsym(RTLD_NEXT, "calloc");
  if (NULL == real_calloc) {
    fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
  }
  real_free = dlsym(RTLD_NEXT, "free");
  if (NULL == real_free) {
    fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
  }
  real_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
  if (NULL == real_posix_memalign) {
    fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
  }
  real_memalign = dlsym(RTLD_NEXT, "memalign");
  if (NULL == real_memalign) {
    fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
  }
  
  // order matters

  char* interval_text = getenv("MTRACE_SAMPLE_INTERVAL");
  if (interval_text){
    sample_interval = atoi(interval_text);
    if (sample_interval <= 0)
      sample_interval = 1;
    sample_rate = 1.0 / sample_interval;
  }

  fp = stderr;
  char* output_dir = getenv("MTRACE_OUTPUT_DIR");
  if (output_dir){
    char filepath[1024];
    snprintf(filepath, 1024, "%s/mtrace.%d.log", output_dir, getpid());
    FILE* tmp_fp = fopen(filepath, "w");
    if (tmp_fp)
      fp = tmp_fp;
  }

  // fprintf(stderr, "r_version = %d, addr=%p\n", _r_debug.r_version, _r_debug.r_brk);
  in_call = 0;
}

  

char** my_backtrace_symbols(void**, int);

static inline uint64_t log_stack(){
  void* bt[256];
  int l = backtrace(bt, 256);
  char** sym = my_backtrace_symbols(bt, l);
  uint64_t ret = 42700991; // nonsence hash algorithm
  if (l){
    for(int i = 0; i < l; ++i){
      ret *= 2357;
      ret ^= (uint64_t) bt[i];
      if(fp) fprintf(fp, "\t%s\n", sym[i]);
    }
  }
  free(sym);
  if(fp) fprintf(fp, "stack %016llx\n", ret);
  return ret;
}

static uint64_t mmtrace_log_stack(void* cli){
  void* ss = mmtrace_stack_begin(cli);
  void* bt[256];
  int l = backtrace(bt, 256);
  char** sym = my_backtrace_symbols(bt, l);
  for(int i = 0; i < l; ++i){
    mmtrace_stack_frame(cli, ss, sym[i]);
  }
  free(sym);
  return mmtrace_stack_commit(cli, ss);
}

__attribute__((visibility("default"))) void* malloc(size_t size)
{
  void *p = NULL;
  if(real_malloc==NULL) {
    mtrace_init();
  }
  if (in_call == 0){
    in_call = 1;
    void* cli = mmtrace_get_client();
    size_t ss = mmtrace_sample_size(cli, size);
    uint64_t hash = 0;
    if (ss){
      hash = mmtrace_log_stack(cli);
    }
    p = real_malloc(size);
    if (ss){
      mmtrace_malloc(cli, p, ss, hash); 
    }
    in_call = 0;
  } else {
    p = real_malloc(size);
  }
  return p;
}

__attribute__((visibility("default"))) void* calloc(size_t nmemb, size_t size){
  void* p = NULL;
  if (real_calloc == NULL){
    // So dlsym uses calloc internally, which will lead to infinite recursion, since our calloc calls dlsym.
    // Apparently dlsym can cope with slightly wrong calloc, see for further explanation:
    // http://blog.bigpixel.ro/2010/09/interposing-calloc-on-linux
    return NULL; // This is a poor implementation of calloc!
  }
  if (in_call == 0){
    in_call = 1;
    void* cli = mmtrace_get_client();
    size_t ss = mmtrace_sample_size(cli, size * nmemb);
    uint64_t hash = 0;
    if (ss){
      hash = mmtrace_log_stack(cli);
    }
    p = real_calloc(nmemb, size);
    if (ss){
      mmtrace_malloc(cli, p, ss, hash); 
    }
    in_call = 0;
  } else {
    p = real_calloc(nmemb, size); 
  }
  return p;
}

__attribute__((visibility("default"))) void* realloc(void* ptr, size_t size){
  void* p = NULL;
  if (real_realloc == NULL){
    mtrace_init();
  }
  if (in_call == 0){
    in_call = 1;
    void* cli = mmtrace_get_client();
    uint64_t hash = 0;
    size_t ss = 0;
    if (size){
      ss = mmtrace_sample_size(cli, size);
      if(ss) {
        hash = mmtrace_log_stack(cli);
      }
    }
    p = real_realloc(ptr, size);
    if (ptr){
      mmtrace_free(cli, ptr);
    }
    if (ss){
      mmtrace_malloc(cli, p, ss, hash);
    }
    in_call = 0;
  } else {
    p = real_realloc(ptr, size); 
  }
  return p;
}

__attribute__((visibility("default"))) void free(void* ptr){
  if (real_free == NULL){
    mtrace_init();
  }
  if (!ptr) return;
  if (in_call == 0){
    in_call = 1;
    void* cli = mmtrace_get_client();
    real_free(ptr);
    mmtrace_free(cli, ptr);
    in_call = 0;
  } else {
    real_free(ptr);
  }
}

__attribute__((visibility("default"))) int posix_memalign(void **memptr, size_t alignment, size_t size){
  int r = 0;
  if (real_posix_memalign == NULL){
    mtrace_init();
  }
  if (in_call == 0){
    in_call = 1;
    void* cli = mmtrace_get_client();
    size_t ss = mmtrace_sample_size(cli, size);

    r = real_posix_memalign(memptr, alignment, size);
    if (r == 0){
      uint64_t hash = 0;
      if (ss){
        hash = mmtrace_log_stack(cli);
      }
      if (ss){
        mmtrace_malloc(cli, *memptr, ss, hash);
      }
    }
    in_call = 0;
  } else {
    r = real_posix_memalign(memptr, alignment, size); 
  }
  return r;
}

__attribute__((visibility("default"))) void* memalign(size_t alignment, size_t size){
  void* p = NULL;
  if (real_memalign == NULL){
    mtrace_init();
  }
  if (in_call == 0){
    in_call = 1;
    void* cli = mmtrace_get_client();
    size_t ss = mmtrace_sample_size(cli, size);
    uint64_t hash = 0;
    if (ss){
      hash = mmtrace_log_stack(cli);
    }
    p = real_memalign(alignment, size);
    if (ss){
        mmtrace_malloc(cli, p, ss, hash);
    }
    in_call = 0;
  } else {
    p = real_memalign(alignment, size); 
  }
  return p;
}

