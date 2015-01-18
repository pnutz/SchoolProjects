/*-----------------------------------------------------------------------------
--  SOURCE FILE:      log.h - A header file to write to a log file output.log
--
--  PROGRAM:          tcp_svr, tcp_clnt
--
--  FUNCTIONS:        File IO
--
--  DATE:             December 2, 2014
--
--  REVISIONS:        (Date and Description)
--
--  DESIGNERS:        Christopher Eng
--
--  PROGRAMMERS:      Christopher Eng
--
--  NOTES:
--  This header file allows programs to write standard and error messages to output.log.
-----------------------------------------------------------------------------*/
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#ifndef LOGFILE
#define LOGFILE "output.log"
#endif /* LOGFILE */

#ifndef LOG_H
#define LOG_H

void logstr (char*);
void logerr (char*);

void logstr (char *message)
{
  FILE *file;
  time_t timer;
  char time_buffer[25];
  struct tm *tm_info;
  struct timeval tv;
  
  file = fopen(LOGFILE, "a");
  if (file == NULL)
  {
    perror("Cannot open log file: output.log");
    exit(1);
  }

  time(&timer);
  tm_info = localtime(&timer);
  strftime(time_buffer, 25, "%D %T", tm_info);
  
  gettimeofday(&tv, 0);

  fprintf(file, "%*s:%i LOG: %s\n", 17, time_buffer, (int)tv.tv_usec % 1000, message);

  fclose(file);
}

void logerr (char *message)
{
  FILE *file;
  time_t timer;
  char time_buffer[25];
  struct tm *tm_info;
  struct timeval tv;

  file = fopen(LOGFILE, "a");
  if (file == NULL)
  {
    perror("Cannot open log file: output.log");
    exit(1);
  }

  time(&timer);
  tm_info = localtime(&timer);
  strftime(time_buffer, 25, "%D %T", tm_info);

  gettimeofday(&tv, 0);

  fprintf(file, "%*s:%i ERR: %s\n", 17, time_buffer, (int)tv.tv_usec % 1000, message);

  fclose(file);
}

#endif /* LOG_H */
