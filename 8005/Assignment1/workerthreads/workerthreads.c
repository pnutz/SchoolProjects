/*----------------------------------------------------------------------------
-- SOURCE FILE: workerthreads.c - A program to measure thread performance
-- 
-- PROGRAM: workerthreads.exe
-- 
-- DATE: January 18, 2015
-- 
-- REVISIONS: (Date and Description)
-- 
-- DESIGNERS: Christopher Eng
-- 
-- PROGRAMMERS: Christopher Eng
-- 
-- NOTES:
-- The program will get its threads to calculate prime number decomposition for a user argument.
-- The program will calculate how long each thread takes to complete the calculation and write to a file.
-----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gmp.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/syscall.h>

#include "primedecompose.h"

#define MAX_FACTORS	1024
#define FILENAME "workerthreads_output.txt"
#define MSGSIZE 50
#define DEFAULTCOUNT 5

int parent(int);
void* child(void *);
long long timeval_diff(struct timeval*, struct timeval*, struct timeval*);

// mutex variable
pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;
char buf[MSGSIZE];
int data_read = 1;

typedef struct
{
  char* Prime;
} ThreadInfo;

int main (int argc, char *argv[]) 
{
  ThreadInfo *info_ptr;
  int count;

  if ((info_ptr = malloc (sizeof (ThreadInfo))) == NULL)
  {
    perror ("malloc");
    exit (1);
  }

  switch (argc)
  {
    case 3:
      count = atoi(argv[2]);
      break;
    case 2:
      count = DEFAULTCOUNT;
      break;
    default:
      fprintf(stderr, "Usage: <number to be factored> <optional: number of threads>\n");
      return 1;
  }

  pthread_t thread_id[count];
  info_ptr->Prime = argv[1];

  // recreate file
  FILE *fp;
  fp = fopen(FILENAME, "w");
  fprintf(fp, "%s\n", "ThreadID  | Calculation Output                       | Execution Time (usec)");
  fprintf(fp, "%s\n", "____________________________________________________________________________");
  fclose(fp);

  int i;
  for (i = 0; i < count; i++)
  {
    pthread_create(&thread_id[i], NULL, child, (void*) info_ptr);
    printf("Created thread %i\n", i);
  }

  parent(count);
  // free info_ptr after parent has received verification that thread finished exec
  free(info_ptr);
  return 0; 
}

int parent(int count)
{
  int value = 0;
  long long result = 0;

  while (value != count)
  { 
    // mutex lock thread for global variable
    pthread_mutex_lock(&time_lock);
   
    if (data_read == 0)
    {
      result += strtoll(buf, NULL, 0);
      data_read = 1;
      value++;
    }

    pthread_mutex_unlock(&time_lock);
  }

  FILE* fp;
  fp = fopen(FILENAME, "a");
  fprintf(fp, "%s\n", "____________________________________________________________________________");
  fprintf(fp, "%*s | %-*s | %lld\n", 9, "0", 40, "Total", result);
  fprintf(fp, "%*s | %-*s | %lld\n", 9, "0", 40, "Average", result/count);
  fclose(fp); 

  return EXIT_SUCCESS;
}

void* child(void *info_ptr)
{
  ThreadInfo* user_info = (ThreadInfo*) info_ptr;
  pid_t thread_id = syscall(SYS_gettid);

  mpz_t dest[MAX_FACTORS];
	mpz_t n;
  int i, l;
  char output[200], calculation[100] = "";
  struct timeval start, end;

  // set start time
  if (gettimeofday(&start, NULL))
  {
    perror("start gettimeofday");
    exit(1);
  }

  // prime computation
  mpz_init_set_str(n, user_info->Prime, 10);
  l = decompose(n, dest);
 
  for(i = 0; i < l; i++) 
	{
    gmp_sprintf(calculation, "%s%s%Zd", calculation, i?" * ":"", dest[i]);
    mpz_clear(dest[i]);
  }

  // format output
  sprintf(output, "%*i | %-*s | ", 9, thread_id, 40, calculation);

  // file io
  FILE *fp;
  fp = fopen(FILENAME, "a");
  fprintf(fp, "%s", output);

  // get end time
  if (gettimeofday(&end, NULL))
  {
    perror("end gettimeofday");
    exit(1);
  }

  // mutex lock thread for global variables, data_read & buf
  int data_saved = 0;
  do
  {
    pthread_mutex_lock(&time_lock);

    // parent thread has read the latest buf value, safe to set it again
    if (data_read == 1)
    {
      // get elapsed time
      sprintf(buf, "%lld", timeval_diff(NULL, &end, &start));
      fprintf(fp, "%s\n", buf);

      data_read = 0;
      data_saved = 1;
      fclose(fp);
    }

    pthread_mutex_unlock(&time_lock);
  } while (data_saved != 1);

  printf("Thread %i finished executing\n", thread_id);
  return 0;
}

// calculate difference in time between end_time and start_time (return usec)
long long timeval_diff(struct timeval *difference, struct timeval *end_time, struct timeval *start_time)
{
  struct timeval temp_diff;

  if (difference == NULL)
  {
    difference = &temp_diff;
  }

  difference->tv_sec = end_time->tv_sec - start_time->tv_sec;
  difference->tv_usec = end_time->tv_usec - start_time->tv_usec;

  while (difference->tv_usec < 0)
  {
    difference->tv_usec += 1000000;
    difference->tv_sec -= 1;
  }

  return 1000000LL * difference->tv_sec + difference->tv_usec;
}
