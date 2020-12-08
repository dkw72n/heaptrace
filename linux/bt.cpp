// backtrace
#include <shared_mutex>
#include <execinfo.h>
#include <map>
#include <string>
#include <cstring>
#include <stdio.h>

// assume that dso's will never be unloaded

typedef std::map<void*, std::string> TyPtr2Sym;
struct SymCache{

  
  void lock(){
    lck.lock();
  }
  
  void unlock(){
    lck.unlock();
  }

  void lock_shared(){
    lck.lock_shared();
  }

  void unlock_shared(){
    lck.unlock_shared();
  }


  const char* get(void* addr) const{
    auto it = store.find(addr);
    if (it == store.end()) return nullptr;
    return it->second.c_str();
  }
  
  const char* set(void* addr, const char* s){
    auto& ret = (store[addr] = s);
    return ret.c_str();
  }

  std::map<void*, std::string> store;
  std::shared_mutex lck;
};

SymCache* get_ptr2sym(){
  static SymCache* _ptr2sym = NULL;
  if (_ptr2sym) return _ptr2sym;
  _ptr2sym = new SymCache();
  return _ptr2sym;
}

extern "C" char** my_backtrace_symbols(void** ptrs, int count){
  void* my_ptrs[256];
  int my_count = 0;
  char** my_ret = NULL;
  char** ret = (char**)malloc(count * sizeof(char*));
  memset(ret, 0, sizeof(char*) * count);
  SymCache* sc = get_ptr2sym();
  sc->lock_shared();
  for(int i = 0; i < count; ++i){
    auto v = sc->get(ptrs[i]);
    if (v){
      ret[i] = (char*) v;
    } else {
      my_ptrs[my_count++] = ptrs[i];
    }
  }
  sc->unlock_shared();
  if (my_count){
    my_ret = backtrace_symbols(my_ptrs, my_count);
    sc->lock();
    for(int j = 0, i = 0; j < count && i < my_count; j++){
      if (ret[j] == NULL){
        auto v = sc->get(ptrs[j]);
        if (v){
          ret[j] = (char*) v;
        } else {
          auto vv = sc->set(ptrs[j], my_ret[i++]);
          ret[j] = (char*) vv;
        }
      }
    }
    sc->unlock();
  }
  free(my_ret);
  return ret;
}
