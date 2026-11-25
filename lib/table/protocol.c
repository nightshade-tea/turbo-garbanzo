#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <net/io.h>
#include <table/protocol.h>

#define MSGBUFSZ 128

static int
parse_server (const char *s, struct server *srv)
{
  char state[sizeof "down"];
  unsigned long conns;

  if (sscanf (s, "%15s %5s %4s %lu", srv->ip, srv->port, state, &conns) != 4)
    return -1;

  if (!strcmp (state, "up"))
    srv->state = 1;

  else if (!strcmp (state, "down"))
    srv->state = 0;

  else
    return -1;

  srv->conns = conns;

  return 0;
}

int
table_protocol_enroll (int sock, const char *ip, const char *port,
                       unsigned long *key)
{
  char msg[MSGBUFSZ], *end;
  unsigned long n;

  snprintf (msg, sizeof msg, "enroll %s %s", ip, port);

  if (net_io_sendln (sock, msg, sizeof msg) < 0)
    return -1;

  if (net_io_recvln (sock, msg, sizeof msg) < 0)
    return -1;

  errno = 0;
  n = strtoul (msg, &end, 10);

  if (end == msg || *end != '\0' || errno == ERANGE)
    return -1;

  *key = n;

  return 0;
}

int
table_protocol_up (int sock, unsigned long key)
{
  char msg[MSGBUFSZ];

  snprintf (msg, sizeof msg, "up %lu", key);

  if (net_io_sendln (sock, msg, sizeof msg) < 0)
    return -1;

  return 0;
}

int
table_protocol_down (int sock, unsigned long key)
{
  char msg[MSGBUFSZ];

  snprintf (msg, sizeof msg, "down %lu", key);

  if (net_io_sendln (sock, msg, sizeof msg) < 0)
    return -1;

  return 0;
}

int
table_protocol_grab (int sock, const char *policy, unsigned long *key,
                     struct server *srv)
{
  char msg[MSGBUFSZ], *end;
  unsigned long n;

  snprintf (msg, sizeof msg, "grab %s", policy);

  if (net_io_sendln (sock, msg, sizeof msg) < 0)
    return -1;

  if (net_io_recvln (sock, msg, sizeof msg) < 0)
    return -1;

  if (!strcmp (msg, "none"))
    return 1;

  errno = 0;
  n = strtoul (msg, &end, 10);

  if (end == msg || *end != ' ' || errno == ERANGE)
    return -1;

  if (parse_server (end + 1, srv) < 0)
    return -1;

  *key = n;

  return 0;
}

int
table_protocol_release (int sock, unsigned long key)
{
  char msg[MSGBUFSZ];

  snprintf (msg, sizeof msg, "release %lu", key);

  if (net_io_sendln (sock, msg, sizeof msg) < 0)
    return -1;

  return 0;
}

int
table_protocol_status (int sock, unsigned long key, struct server *srv)
{
  char msg[MSGBUFSZ];

  snprintf (msg, sizeof msg, "status %lu", key);

  if (net_io_sendln (sock, msg, sizeof msg) < 0)
    return -1;

  if (net_io_recvln (sock, msg, sizeof msg) < 0)
    return -1;

  return parse_server (msg, srv);
}

int
table_protocol_total (int sock, unsigned long *total)
{
  char msg[MSGBUFSZ], *end;
  unsigned long n;

  if (net_io_sendln (sock, "total", sizeof "total") < 0)
    return -1;

  if (net_io_recvln (sock, msg, sizeof msg) < 0)
    return -1;

  errno = 0;
  n = strtoul (msg, &end, 10);

  if (end == msg || *end != '\0' || errno == ERANGE)
    return -1;

  *total = n;

  return 0;
}
