/*-----------------------------------------------------------------------------
--  SOURCE FILE:      config.h - A header file to read a config file
--
--  PROGRAM:          tcp_clnt, tcp_svr, tcp_network
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
--  The header file will allow programs to read from a parameter filename in key<whitespace>value format
-----------------------------------------------------------------------------*/
#include <stdio.h>
#include <errno.h>

#ifndef MAXLINE
#define MAXLINE 30
#endif /* MAXLINE */

// config_kv holds a key-value configuration
typedef struct
{
  char key[MAXLINE];
  char value[MAXLINE];
} ConfigKv;

// config_data stores 'size' # of config_kvs:w
typedef struct
{
  int size;
  ConfigKv kv[100];
} ConfigData;

ConfigData parse(char *filename)
{
  FILE *file;
  if ((file = fopen(filename, "r")) == NULL)
  {
    printf("Can't open config file: %s", filename);
    exit(1);
  }

  ConfigData config_file;
  char key[MAXLINE];
  char val[MAXLINE];
  int count = 0;
 
  // pull first line of config file to ignore
  char read[1000];
  fgets(read, 1000, file);
 
  while (fscanf(file, "%s %s\n", key, val) != EOF && count != 100)
  {
    if (strlen(val) == 0)
    {
      printf("Invalid key-value configuration for config file\n");
      exit(1);
    }
    sprintf(config_file.kv[count].key, "%s", key);
    sprintf(config_file.kv[count].value, "%s", val);
    count++;
    
    memset(key, 0, MAXLINE);
    memset(val, 0, MAXLINE);
  }

  // print warning if count reaches 100
  if (count == 100)
  {
    printf("Warning: config.h has found 100 key-value configurations. Any configurations past 100 will not be added to config_data.\n");
  }
  config_file.size = count;
  return config_file;
}
