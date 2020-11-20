#include <string.h>
#undef memcpy
void* memcpy_old(void * dest, const void* src, size_t size);
#ifdef __i386__
    __asm__(".symver glob_old,glob@GLIBC_2.1");
#elif defined(__amd64__)
    __asm__(".symver memcpy_old,memcpy@GLIBC_2.2.5");
#elif defined(__arm__)
    __asm(".symver glob_old,glob@GLIBC_2.4");
#elif defined(__aarch64__)
    __asm__(".symver glob_old,glob@GLIBC_2.17");
#endif
void* __wrap_memcpy(void * dest, const void* src, size_t size)
{
    return memcpy_old(dest, src, size);
}
