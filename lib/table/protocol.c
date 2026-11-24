#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <net/io.h>
#include <table/protocol.h>

int
get (int sock, unsigned int key, unsigned long *val)
{
  char msg[64], *end;
  unsigned long n;

  snprintf (msg, sizeof msg, "get %u", key);

  if (net_io_sendln (sock, msg, sizeof msg) < 0)
    return -1;

  if (net_io_recvln (sock, msg, sizeof msg) < 0)
    return -1;

  errno = 0;
  n = strtoul (msg, &end, 10);

  if (end == msg || *end != '\0' || errno == ERANGE)
    return -1;

  *val = n;
  return 0;
}

int
set (int sock, unsigned int key, unsigned long val)
{
  char msg[64];

  snprintf (msg, sizeof msg, "set %u %lu", key, val);

  if (net_io_sendln (sock, msg, sizeof msg) < 0)
    return -1;

  return 0;
}

int
inc (int sock, unsigned int key)
{
  char msg[64];

  snprintf (msg, sizeof msg, "inc %u", key);

  if (net_io_sendln (sock, msg, sizeof msg) < 0)
    return -1;

  return 0;
}

int
dec (int sock, unsigned int key)
{
  char msg[64];

  snprintf (msg, sizeof msg, "dec %u", key);

  if (net_io_sendln (sock, msg, sizeof msg) < 0)
    return -1;

  return 0;
}
