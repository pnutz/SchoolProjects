/*----------------------------------------------------------------------------
-- SOURCE FILE: workerprocs.c - A program to measure child process performance
-- 
-- PROGRAM: workerprocs.exe
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
-- The program will get its child processes to calculate prime number decomposition for a user argument.
-- The program will calculate how long each child process takes to complete the calculation and write to a file.
-----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <gmp.h>
#include <sys/time.h>

#include "primedecompose.h"

#define MAX_FACTORS	1024
#define FILENAME "workerprocs_output.txt"
#define MSGSIZE 50
#define DEFAULTCOUNT 5

int parent(int, int[]);
int child(char*, int[]);
long long timeval_diff(struct timeval*, struct timeval*, struct timeval*);

int main (int argc, char *argv[]) 
{
  pid_t childpid = 0; 
  int pfd[2];
  int count;

  switch (argc)
  {
    case 3:
      count = atoi(argv[2]);
      break;
    case 2:
      count = DEFAULTCOUNT;
      break;
    default:
      fprintf(stderr, "Usage: <number to be factored> <optional: number of child processes>\n");
      return 1;
  }

  // recreate file
  FILE *fp;
  fp = fopen(FILENAME, "w");
  fprintf(fp, "%s\n", "ProcessID | Calculation Output                       | Execution Time (usec)");
  fprintf(fp, "%s\n", "____________________________________________________________________________");
  fclose(fp);

  // open pipe
  if (pipe(pfd) < 0)
  {
    perror("pipe call");
    exit(1);
  }

  int i;
  for (i = 0; i < count; i++)
  {
    childpid = fork();
    if (childpid == 0) // child
    {
     	fprintf(stderr, "Created child process %ld\n", (long) getpid());

      child(argv[1], pfd);
      break;
    }
    else if (childpid < 0) // error occurred
    {
      perror("fork failed\n");
      return 1;
    }
  }

  if (childpid > 0)
  {
    parent(count, pfd);
    printf("Parent process finished executing\n");
    exit(0);
  }

  return 1;
}

int parent(int count, int p[2])
{
  char buf[MSGSIZE];
  long long result = 0;

  // close write descriptor 
  close(p[1]);

  int i;
  for (i = 0; i < count; i++)
  {
    if (read(p[0], buf, MSGSIZE) > 0)
    {
      result += strtoll(buf, NULL, 0);
    }
  }

  FILE* fp;
  fp = fopen(FILENAME, "a");
  fprintf(fp, "%s\n", "____________________________________________________________________________");
  fprintf(fp, "%*s | %-*s | %lld\n", 9, "0", 40, "Total", result);
  fprintf(fp, "%*s | %-*s | %lld\n", 9, "0", 40, "Average", result/count);
  fclose(fp); 

  return EXIT_SUCCESS;
}

int child(char* prime, int p[2])
{
  mpz_t dest[MAX_FACTORS];
	mpz_t n;
  int i, l;
  char output[200], calculation[100] = "", diff[MSGSIZE];
  struct timeval start, end;

  // close read descriptor
  close(p[0]);

  // set start time
  if (gettimeofday(&start, NULL))
  {
    perror("start gettimeofday");
    exit(1);
  }

  // prime computation
  mpz_init_set_str(n, prime, 10);
  l = decompose(n, dest);
 
  for(i = 0; i < l; i++) 
	{
    gmp_sprintf(calculation, "%s%s%Zd", calculation, i?" * ":"", dest[i]);
    mpz_clear(dest[i]);
  }

  // get end time
  if (gettimeofday(&end, NULL))
  {
    perror("end gettimeofday");
    exit(1);
  }

  // get elapsed time
  sprintf(diff, "%lld", timeval_diff(NULL, &end, &start));
  // format output
  sprintf(output, "%*ld | %-*s | %s", 9, (long) getpid(), 40, calculation, diff);
  
  // file io
  FILE *fp;
  fp = fopen(FILENAME, "a");
  fprintf(fp, "%s\n", output);
  fclose(fp);

  printf("Child process %ld finished executing\n", (long) getpid());
  // send elapsed time over pipe to parent
  write (p[1], (char*) diff, MSGSIZE);

  return EXIT_SUCCESS;
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
