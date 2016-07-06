#include <stdio.h>
#include <linux/kvm.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

int main (int argc, char *argv[])
{
   int kvm_fd;
   int ret;
   volatile int loop=1;


   kvm_fd = open("/dev/kvm", O_RDWR);

   if (kvm_fd < 0)
      printf("issue with open - err no %d \n", errno);

   ret = ioctl(kvm_fd, 0xb1, 0);

   printf("return value %d, error no %d\n", ret, errno);

   while (loop) {
      sleep(60000);
   };

   printf("return value %d\n", ret);

   return 0;
}

