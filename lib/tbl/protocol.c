#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <net/io.h>
#include <tbl/protocol.h>

static int
parse_ulong (const char *s, char term, unsigned long *n, char **end)
{
  unsigned long v;
  char *e;

  errno = 0;
  v = strtoul (s, &e, 10);

  if (errno == ERANGE)
    return -1;

  if (e == s || *e != term)
    {
      errno = EBADMSG;
      return -1;
    }

  *n = v;

  if (end != NULL)
    *end = e;

  return 0;
}

static int
parse_server (const char *s, struct server *srv)
{
  char state[sizeof "down"];
  unsigned long conns;

  if (sscanf (s, "%15s %5s %4s %lu", srv->ip, srv->port, state, &conns) != 4)
    {
      errno = EBADMSG;
      return -1;
    }

  if (strcmp (state, "up") == 0)
    srv->state = 1;

  else if (strcmp (state, "down") == 0)
    srv->state = 0;

  else
    {
      errno = EBADMSG;
      return -1;
    }

  srv->conns = conns;

  return 0;
}

static int
ok_cmd (int fd, const char *cmd, unsigned long key)
{
  char msg[TBL_MSGBUFSZ];

  snprintf (msg, sizeof msg, "%s %lu", cmd, key);

  if (net_io_sendln (fd, msg, sizeof msg) < 0)
    return -1;

  if (net_io_recvln (fd, msg, sizeof msg) < 0)
    return -1;

  if (strcmp (msg, "ok") != 0)
    {
      errno = EBADMSG;
      return -1;
    }

  return 0;
}

int
tbl_enroll (int fd, const char *ip, const char *port, unsigned long *key)
{
  char msg[TBL_MSGBUFSZ];

  snprintf (msg, sizeof msg, "enroll %s %s", ip, port);

  if (net_io_sendln (fd, msg, sizeof msg) < 0)
    return -1;

  if (net_io_recvln (fd, msg, sizeof msg) < 0)
    return -1;

  return parse_ulong (msg, '\0', key, NULL);
}

int
tbl_up (int fd, unsigned long key)
{
  return ok_cmd (fd, "up", key);
}

int
tbl_down (int fd, unsigned long key)
{
  return ok_cmd (fd, "down", key);
}

int
tbl_grab (int fd, const char *policy, unsigned long *key, struct server *srv)
{
  char msg[TBL_MSGBUFSZ], *end;
  unsigned long n;

  snprintf (msg, sizeof msg, "grab %s", policy);

  if (net_io_sendln (fd, msg, sizeof msg) < 0)
    return -1;

  if (net_io_recvln (fd, msg, sizeof msg) < 0)
    return -1;

  if (strcmp (msg, "none") == 0)
    return 1;

  if (parse_ulong (msg, ' ', &n, &end) < 0)
    return -1;

  if (parse_server (end + 1, srv) < 0)
    return -1;

  *key = n;

  return 0;
}

int
tbl_release (int fd, unsigned long key)
{
  return ok_cmd (fd, "release", key);
}

int
tbl_status (int fd, unsigned long key, struct server *srv)
{
  char msg[TBL_MSGBUFSZ];

  snprintf (msg, sizeof msg, "status %lu", key);

  if (net_io_sendln (fd, msg, sizeof msg) < 0)
    return -1;

  if (net_io_recvln (fd, msg, sizeof msg) < 0)
    return -1;

  return parse_server (msg, srv);
}

int
tbl_total (int fd, unsigned long *total)
{
  char msg[TBL_MSGBUFSZ];

  if (net_io_sendln (fd, "total", sizeof "total") < 0)
    return -1;

  if (net_io_recvln (fd, msg, sizeof msg) < 0)
    return -1;

  return parse_ulong (msg, '\0', total, NULL);
}
