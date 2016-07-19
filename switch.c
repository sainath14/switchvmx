#define _GNU_SOURCE
#include <stdio.h>
#include <linux/kvm.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sched.h>
#include <pthread.h>

#define NUM_PROC 4

bool check_if_daemon(int argc, char *argv[]);
void run_daemon(void);
void parse_client_request(int fd, FILE *fp);
void run_client(int argc, char *argv[]);
void parse_cpu_range(char *range, int *ptr);
void* switch_and_exit (void* args);
void switch_procs_in (int from, int to);
void switch_procs_out (int from, int to);
int * collect_procs_in_non_root(void);
void parse_ranges_and_print(char *buffer);

pthread_t threads[NUM_PROC];
pthread_cond_t cond[NUM_PROC];
pthread_mutex_t mutex[NUM_PROC];
bool in_out[NUM_PROC];

FILE *daemon_fp;
int kvm_fd, log_fd;

int main (int argc, char *argv[])
{
   int kvm_fd;
   int ret;
   volatile int loop=1;
   bool flag;

   flag = check_if_daemon(argc, argv);

   if (flag == true) {
      printf("Its a daemon invocation\n");
      run_daemon();
      printf("daemon invocation should not come here\n");
      exit(1);
   } else {
     printf("its a client invocation\n");
     run_client(argc, argv);
     exit(0);
   }
   return 0;
}

void* switch_and_exit (void* args)
{
   int ret;
   volatile int loop=1;
   FILE* fp;
   bool arg = true;

   int *index_ptr = (int *)args;

//   fp = fopen("/var/lib/switch", "w+");

//   kvm_fd = open("/dev/kvm", O_RDWR);

//   if (kvm_fd < 0)
//      fprintf(daemon_fp, "issue with open - err no %d \n", errno);
   fp = fdopen(log_fd, "a");
   fprintf(fp, "kvm file descriptor  %d\n", kvm_fd);
   fflush(fp);

   ret = ioctl(kvm_fd, 0xb1, (void *)&arg);
   

   fprintf(fp, "return value of ioctl %d, error no %d\n", ret, errno);
   fprintf(fp, "cpu index %d\n", *index_ptr);
   fflush(fp);

   pthread_mutex_lock(&mutex[*index_ptr]);
   pthread_cond_wait(&cond[*index_ptr], &mutex[*index_ptr]);
   pthread_mutex_unlock(&mutex[*index_ptr]);

/*

   while (loop) {
      sleep(60000);
   };
*/

   arg = false;
   ret = ioctl(kvm_fd, 0xb1, (void *)&arg);

   fprintf(fp, "return value %d\n", ret);

   return 0;
}

bool check_if_daemon(int argc, char *argv[]) 
{
    int opt;

    if (argc < 1)
       return false;

    while((opt = getopt(argc, argv, "d")) != -1) {

      switch(opt) {
        case 'd' : 
           return true;
        default :
           continue;
      }
   }

   return false;
}


void run_daemon(void)
{
   pid_t pid;
   FILE *fp = NULL;
   pid_t sid = 0;
   int sockfd,newfd;
   struct sockaddr_un addr;

   pid = fork();

   if (pid < 0) {
      printf("Fork a daemon process failed\n");
      exit(1);
   }

   if (pid > 0) {
      //exit the parent process successfully
      printf("Fork succeeded\n");
      exit(0);
   }

   sid = setsid();

   if (sid < 0) {
      printf("setsid failed\n");
      exit(1);
   }

   chdir("/");

   close(STDIN_FILENO);
   close(STDOUT_FILENO);
   close(STDERR_FILENO);

   fp = fopen("/var/lib/switch", "w+");
   daemon_fp = fp;
   log_fd = fileno(fp);

   kvm_fd = open("/dev/kvm", O_RDWR);
   if (kvm_fd < 0)
      fprintf(fp, "error in opening kvm device file\n");
   fprintf(fp, "kvm device file descriptor %d\n", kvm_fd);
      

   sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

   if (sockfd < 0) {
      fprintf(fp, "Socket Creation failed\n");
      exit(1);
   }

   memset((void *)&addr, 0, sizeof(addr));
   addr.sun_family = AF_UNIX;
   strncpy(addr.sun_path, "/var/lib/switch.sock", sizeof(addr.sun_path)-1);
   if (bind(sockfd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
      fprintf(fp, "error on binding unix file to socket\n");
      exit(1);
   }

   if (listen(sockfd, 1) < 0) {
      fprintf(fp, "error on listening to socket\n");
      exit(1);
   }

   while (1) {
       newfd = accept(sockfd, NULL, NULL);
       parse_client_request(newfd, fp);
       fprintf(fp, "Logging info");
       fflush(fp);
   }

   unlink("/var/lib/switch.sock");
   fclose(fp);
   exit(0);
}

void parse_client_request(int fd, FILE* fp)
{
   char buffer[100];
   int n, num_of_ranges, iter, from, to;
   int in_out;
   int *range_ptr;
   int* out;

   memset((void *)buffer, 0, 100);
   n = read(fd, buffer, 100);

   num_of_ranges = *(int *)buffer;
   iter = 0;

   in_out = *((int *)buffer + 1);
   range_ptr = (int *)buffer+2;

   if (in_out == 2) {
      out = collect_procs_in_non_root();
      fprintf(fp, "sending out ranges of non-root procs\n");
      write(fd, (char *)out, 100);
   } else {

     fprintf(fp, "num of ranges %d\n", num_of_ranges);
      while (iter < num_of_ranges) {
         fprintf(fp, "range %d:\n", iter);
         from = *(range_ptr+2*iter);
         to =  *(range_ptr+(2*iter+1));
         fprintf(fp, "FROM: %d\n", from);
         fprintf(fp, "TO: %d\n", to);
         if (in_out == 1) {
            fprintf(fp, "calling switch_procs_in\n");
            switch_procs_in(from, to);
         }
         else if (in_out == 0) {
            fprintf(fp, "calling switch_procs_out\n");
            switch_procs_out(from, to);
         }
         iter++;
     }
  }

   fprintf(fp, "read %d bytes from socket\n", n);

   close(fd);   
}

int * collect_procs_in_non_root(void)
{
   int index = 0;
   int *buffer, *ranges;
   bool range_started;
   
   buffer = (int *)malloc(100);
   buffer[0] = 0;
   ranges = buffer+1;
   range_started = false;
   for (index = 0; index < NUM_PROC; index++) {
       if (in_out[index] == true) {
          if (!range_started) {
             range_started = true;
             buffer[0]++;
             *ranges++ = index;
           }
       } else {
           if (range_started) {
              *ranges++ = index-1;
              range_started = false;
           }
       }
   }
   *buffer='\n';
   return buffer;
}

void switch_procs_in(int from, int to)
{
   int index;
   cpu_set_t cpus;
   pthread_attr_t attr;
   int *ptr;
   
   ptr = malloc(((to-from)+1)*sizeof(int));

   pthread_attr_init(&attr);
   for (index = from; index <= to; index++) {
      CPU_ZERO(&cpus);
      CPU_SET(index, &cpus);
      pthread_cond_init(&cond[index], NULL);
      pthread_mutex_init(&mutex[index], NULL);
      pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
      *ptr = index;
      in_out[index] = true;
      pthread_create(&threads[index], &attr, switch_and_exit,(void *)ptr);
      ptr++;
   }
}

void switch_procs_out(int from, int to)
{
   int index;
   cpu_set_t cpus;
   pthread_attr_t attr;
   int *ptr;
   
   for (index = from; index <=to; index++) {
       in_out[index] = false;
       pthread_mutex_lock(&mutex[index]);
       pthread_cond_signal(&cond[index]);
       pthread_mutex_unlock(&mutex[index]);
   }
}
void run_client(int argc, char *argv[])
{
  int sockfd, n, *pointer, size, *range_ptr, in_out;
  struct sockaddr_un addr;
 

  sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

  if (sockfd < 0) {
     printf("Socket Creation failed\n");
     exit(1);
  }

  memset((void *)&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, "/var/lib/switch.sock", sizeof(addr));

  if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
     printf("not able to connect to unix socket\n");
     exit(1);
  }

  if (argc < 2) {
     printf("Incorrect number of arguments - Need the cpu or cpu range and in/out of non-root mode \n");
     exit(1);
  }

  if (!(strcmp(argv[1], "in")))
       in_out = 1;
  else if (!(strcmp(argv[1], "out")))
       in_out = 0;
  else if (!(strcmp(argv[1], "ls")))
       in_out = 2;
  else {
       printf("argument 1 should be either \"in\" or \"out\" \"ls\"\n");
       exit(1);
  }

  if (in_out != 2) {
     n = 2;
     size = ((argc-2)*2 + 2)*sizeof(int);
     pointer = malloc(size);
     memset((void *)pointer, 0, size);
     pointer[0] = argc-2;
     pointer[1] = in_out;

     range_ptr = pointer + 2;
     while (n < argc) {
         if (strchr(argv[n], '-')) {
            parse_cpu_range(argv[n], (range_ptr+(2*(n-2))));
         }
        n++;
     }
  } else {
    size = 2*sizeof(int);
    pointer = malloc(size);
    pointer[0] = 0;
    pointer[1] = 2;
  }

  if (write(sockfd, pointer, (size_t)size) == (size_t) size) {
     printf("All data is written\n");
  }

  char *buffer = malloc(100);
  memset(buffer, 0, 100);

  if (in_out == 2) {
     printf("read on socket for ranges\n");
     n = read(sockfd, buffer, 100);
     if (n < 0)
        printf("read on socket returned error\n");
     printf("num of bytes read %d\n", n);
     parse_ranges_and_print(buffer);
  }

  close(sockfd);
  exit(0);
}

void parse_ranges_and_print(char *buffer)
{
   int *pointer, *pointer_ranges;
   int num_ranges =0;
   int index;
   pointer = (int *)buffer;

   printf("%s", buffer);

   num_ranges = pointer[0];
   pointer_ranges = pointer+1;
   for (index = 0; index < num_ranges; index++) {
       printf("%d", pointer_ranges[2*index]);
       printf("-");
       printf("%d", pointer_ranges[2*index+1]);  
   }
}

void parse_cpu_range(char *range, int *ptr)
{
   char *string = NULL;
   char *from, *to;

   string = strdup(range);
   if (string != NULL) {
      from = strsep(&string, "-");
      ptr[0] = atoi(from);
      printf("from %d\n", ptr[0]);

      to = strsep(&string, "-"); 
      ptr[1] = atoi(to);
      printf("to %d\n", ptr[1]);
   }
}
