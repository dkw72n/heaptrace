#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "sampler.h"
#include <unordered_set>
#include <string>
#include <ctime>
// #include <cstring>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "mmtrace.h"
#include <pthread.h>

static int64_t now_us() {
#if defined(HAVE_POSIX_CLOCKS)
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000LL + t.tv_nsec / 1000;
#else
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000000LL + t.tv_usec;
#endif
}
unsigned long long current_usecond();

const uint64_t FNV_PRIME = 1099511628211ULL;
const uint64_t FNV_BIAS = 14695981039346656037ULL;

static uint64_t fnv1a(const char* s){
  uint64_t hash = FNV_BIAS;
  while (*s){
    hash *= FNV_PRIME;
    hash ^= *s++;
  }
  return hash;
}

struct _StackSession{
  std::string stack;
  uint64_t append(const char* t){
    char buf[32];
    uint64_t ret = fnv1a(t);
    sprintf(buf, "|%016llx", ret);
    stack += buf;
    return ret;
  }
  uint64_t hash(){
    return fnv1a(stack.c_str());
  }
};

struct _Mmtrace{
  int version;
  FILE* stacks_fp;
  FILE* mmtrace_fp;
  perfetto::profiling::Sampler sampler;

  std::unordered_set<void*> _allocated;
  std::unordered_set<uint64_t> _frames;
  std::unordered_set<uint64_t> _stacks;
  pid_t _pid;

  _Mmtrace(int sample_interval):sampler(sample_interval){
    char buf[256];
    _pid = getpid();
    sprintf(buf, "MallocMon/%d/hs/stacks.%d.txt", _pid, _pid);
    stacks_fp = fopen(buf, "w");
    if (!stacks_fp){
      fprintf(stderr, "open(%s) failed: %d\n", buf, errno);
      // sleep(1000);
    }
    sprintf(buf, "MallocMon/%d/hs/mmtrace.%d.txt", _pid, _pid);
    mmtrace_fp = fopen(buf, "w");
    if (!mmtrace_fp){
      fprintf(stderr, "open(%s) failed: %d\n", buf, errno);
      // sleep(1000);
    }
  }

  ~_Mmtrace(){
    if (mmtrace_fp){
      fclose(mmtrace_fp);
      mmtrace_fp = NULL;
    }
    if (stacks_fp){
      fclose(stacks_fp);
      stacks_fp = NULL;
    }
  }
  
  bool ok(){
    return stacks_fp && mmtrace_fp;
  }

  void* stack_begin(){
    return (void*) new _StackSession();
  }
  
  void stack_frame(void* session, const char* frame){
    _StackSession* ss = (_StackSession*)session;
    uint64_t frame_hash = ss->append(frame);
    if (_frames.find(frame_hash) == _frames.end()){
      fprintf(stacks_fp, "# %016llx %s\n", frame_hash, frame);
      _frames.insert(frame_hash);
    }
  }
  
  uint64_t stack_commit(void* session){
    _StackSession* ss = (_StackSession*)session;
    uint64_t ret = ss->hash();
    if (_stacks.find(ret) == _stacks.end()){
      fprintf(stacks_fp, "> %016llx %s\n", ret, ss->stack.c_str());
      _stacks.insert(ret);
      fflush(stacks_fp);
    }
    delete ss;
    return ret;
  }

  void malloc(void* ptr, size_t n, uint64_t stack){
    // 32283 00001000 00000000ea6c4000 0000000000005000 90675279bb28841d 0005a94755386e22
    fprintf(mmtrace_fp, "%d %08x %016llx %016llx %016llx %016llx\n", _pid, 0x1000, (uint64_t)ptr, (uint64_t)n, stack, now_us());
    _allocated.insert(ptr);
  }

  void free(void* ptr){
    if (_allocated.find(ptr) != _allocated.end()){
      fprintf(mmtrace_fp, "%d %08x %016llx %016llx %016llx %016llx\n", _pid, 0x8000, (uint64_t)ptr, (uint64_t)0, (uint64_t)0, now_us());
      _allocated.erase(ptr);
      fflush(mmtrace_fp);
    }
  }
  
  size_t sample_size(size_t n){
    return sampler.SampleSize(n);
  }
};

int is_dir(const char *path)
{
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISDIR(path_stat.st_mode);
}


static _Mmtrace* cur_instance = NULL;

void atfork(void){
  cur_instance = NULL;
}

void __attribute__((constructor)) setup(){
  pthread_atfork(NULL, NULL, atfork);
}

void* mmtrace_get_client(){
  static pid_t cur_pid = -1;
  if (cur_instance != NULL){
    return (void*)cur_instance;
  }
  char dirname[256];
  const char* interval = getenv("MTRACE_SAMPLE_INTERVAL");
  int i = 1024;
  if (interval){
    i = atoi(interval);
    if (i < 1){
      i = 32;
    } else if ( i > 65536) {
      i = 65536;
    }
  }
  mkdir("MallocMon", 0666);
  sprintf(dirname, "MallocMon/%d", getpid());
  mkdir(dirname, 0666);
  sprintf(dirname, "MallocMon/%d/hs", getpid());
  mkdir(dirname, 0666);
  if (is_dir(dirname)){
    _Mmtrace* ret = new _Mmtrace(i);
    if (ret->ok()){
      cur_instance = ret;
      return (void*) ret;
    }
    delete ret;
  }
  return NULL;
}

void* mmtrace_stack_begin(void* cli){
  _Mmtrace* mt = (_Mmtrace*) cli;
  if (!cli) return NULL;
  return mt->stack_begin();
}

void mmtrace_stack_frame(void* cli, void* session, const char* frame){
  _Mmtrace* mt = (_Mmtrace*) cli;
  if (!cli) return;
  mt->stack_frame(session, frame);
}

uint64_t mmtrace_stack_commit(void* cli, void* session){
  _Mmtrace* mt = (_Mmtrace*) cli;
  if (!cli) return 0;
  return mt->stack_commit(session);
}

void mmtrace_malloc(void* cli, void* ptr, size_t n, uint64_t stack){
  _Mmtrace* mt = (_Mmtrace*) cli;
  if (!cli) return;
  mt->malloc(ptr, n, stack);
}

void mmtrace_free(void* cli, void* ptr){
  _Mmtrace* mt = (_Mmtrace*) cli;
  if (!cli) return;
  mt->free(ptr);
}

size_t mmtrace_sample_size(void* cli, size_t n){
  _Mmtrace* mt = (_Mmtrace*) cli;
  if (!cli) return 0;
  return mt->sample_size(n); 
}
