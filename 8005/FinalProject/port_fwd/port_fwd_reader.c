#include <stdio.h>

#define PORT_FWD_TABLE "port_fwd_table.config"
#define MAX_PORT_CHAR 5
#define MAX_ADDR_CHAR 300
#define MAX_CONFIG_NUM 100

struct PortForward {
  int fd;
  int rcv_port;
  int svr_port;
  char* svr_addr;
} PortForward;

int num_port_fwd = 0;
struct PortForward *port_config;

// returns 1 if a rcv_port match is found within first count indices of port_config
// returns 0 if not
int checkRcvPortExists(int rcv_port, int count)
{
  int i;
  for (i = 0; i < count; i++)
  {
    if (port_config[i].rcv_port == rcv_port)
    {
      return 1;
    }
  }
  return 0;
}

// read PORT_FWD_TABLE and store in PortForward port_config
// returns number of port configurations or -1 if error
int readPortFwdTable()
{
  char rcv_port[MAX_CONFIG_NUM][MAX_PORT_CHAR];
  char svr_port[MAX_CONFIG_NUM][MAX_PORT_CHAR];
  char svr_addr[MAX_CONFIG_NUM][MAX_ADDR_CHAR];

  FILE *file;
  if ((file = fopen(PORT_FWD_TABLE, "r")) == NULL)
  {
    printf("Can't open port forward table: %s\n", PORT_FWD_TABLE);
    return -1;
  }

  // pull first line of file to ignore
  char read[1000];
  fgets(read, 1000, file);

  while (fscanf(file, "%[^=]%*c%[^|]%*c%s\n", rcv_port[num_port_fwd], svr_addr[num_port_fwd], svr_port[num_port_fwd]) != EOF && num_port_fwd != 100)
  {
    if (strlen(rcv_port[num_port_fwd]) == 0 || strlen(svr_addr[num_port_fwd]) == 0 || strlen(svr_port[num_port_fwd]) == 0)
    {
      printf("Warning: Line %i has an invalid port-forward configuration\n", num_port_fwd + 2);
      break;
    }
    
    num_port_fwd++;
  }

  if (num_port_fwd == 0)
  {
    printf("No port forward configurations found\n");
    return -1;
  }
  else if (num_port_fwd == 100)
  {
    printf("Stopped adding port-forward configurations at 100 forwarded ports\n");
  }

  // store config information at port_config
  if ((port_config = malloc(sizeof(struct PortForward) * num_port_fwd)) == NULL)
  {
    printf("PortForward malloc error\n");
    return -1;
  }

  int i, insert_index = 0;
  for (i = 0; i < num_port_fwd; i++)
  {
    // only add unique rcv_port configurations to port_config
    int tmp_rcv_port = atoi(rcv_port[i]);
    if (checkRcvPortExists(tmp_rcv_port, insert_index) == 0)
    {
      port_config[insert_index].rcv_port = tmp_rcv_port;
      port_config[insert_index].svr_port = atoi(svr_port[i]);
      port_config[insert_index].svr_addr = svr_addr[i];
      insert_index++;
    }
  }

  // realloc port_config if it allocated too much memory the first time
  if (insert_index < num_port_fwd)
  {
    num_port_fwd = insert_index;
    if ((port_config = realloc(port_config, sizeof(struct PortForward) * num_port_fwd)) == NULL)
    {
      printf("PortForward realloc error\n");
      return -1;
    }
  }

  return num_port_fwd;
}

void freePortFwdTable()
{
  free(port_config);
}
