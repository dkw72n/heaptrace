#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
int main(){
  for(int i = 0; i < 3; ++i){
    void* a = malloc(10000);
    usleep(1000);
    free(a);
    fork();
    printf("%d\n", i);
  }
  return 0;
}
