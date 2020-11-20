// backtrace
#include <shared_mutex>
#include <execinfo.h>
#include <map>
#include <string>
#include <cstring>
#include <stdio.h>

// assume that dso's will never be unloaded
static std::map<void*, std::string> ptr2sym;
static std::shared_mutex lock;

extern "C" char** my_backtrace_symbols(void** ptrs, int count){
  void* my_ptrs[256];
  int my_count = 0;
  char** my_ret = NULL;
  char** ret = (char**)malloc(count * sizeof(char*));
  memset(ret, 0, sizeof(char*) * count);

  lock.lock_shared();
  for(int i = 0; i < count; ++i){
    auto it = ptr2sym.find(ptrs[i]);
    if (it == ptr2sym.end()){
      my_ptrs[my_count++] = ptrs[i];
    } else {
      ret[i] = (char*)it->second.c_str();
    }
  }
  lock.unlock_shared();
  if (my_count){
    my_ret = backtrace_symbols(my_ptrs, my_count);
    lock.lock();
    for(int j = 0, i = 0; j < count && i < my_count; j++){
      if (ret[j] == NULL){
        auto& inserted = (ptr2sym[ptrs[j]] = my_ret[i++]);
        ret[j] = (char*) inserted.c_str();
      }
    }
    lock.unlock();
  }
  free(my_ret);
  return ret;
}
