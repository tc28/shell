#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

int main() {
 int k = 0;
 while (k <= 10) {
   printf("running\n");
   usleep(500000);
   k++;
 }
 return 0;
}
