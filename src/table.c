#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <log/pr.h>
#include <net/io.h>
#include <net/tcp.h>
#include <tbl/protocol.h>

static struct server table[TBL_LENGTH];
static _Atomic unsigned long count, total;

/* guards enroll, grab, release and rr  */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long rr;

static int
parse_key (const char *p, unsigned long *key)
{
  unsigned long n;
  char *end;

  errno = 0;
  n = strtoul (p, &end, 10);

  if (*end != '\0' || errno == ERANGE || n >= count)
    return -1;

  *key = n;

  return 0;
}

static int
enroll_handler (int connfd, char msg[TBL_MSGBUFSZ])
{
  char ip[TBL_ADDRSTRLEN], port[TBL_PORTSTRLEN], *p, *sp;
  size_t iplen, portlen;
  unsigned long key, n;

  p = msg + sizeof "enroll";

  if ((sp = strchr (p, ' ')) == NULL)
    return -1;

  iplen = (size_t)(sp - p);
  portlen = strlen (sp + 1);

  if (iplen == 0 || iplen >= sizeof ip || portlen == 0
      || portlen >= sizeof port)
    return -1;

  memcpy (ip, p, iplen);
  ip[iplen] = '\0';
  memcpy (port, sp + 1, portlen + 1);

  pthread_mutex_lock (&lock);
  n = count;

  for (key = 0; key < n; ++key)
    if (!strcmp (table[key].ip, ip) && !strcmp (table[key].port, port))
      break;

  if (key == n)
    {
      if (n == TBL_LENGTH)
        {
          pthread_mutex_unlock (&lock);
          return -1;
        }

      memcpy (table[key].ip, ip, sizeof ip);
      memcpy (table[key].port, port, sizeof port);

      /* publish only after the entry is filled  */
      count = n + 1;
    }

  pthread_mutex_unlock (&lock);

  snprintf (msg, TBL_MSGBUFSZ, "%lu", key);

  if (net_io_sendln (connfd, msg, TBL_MSGBUFSZ) < 0)
    return -1;

  return 0;
}

static int
up_handler (int connfd, char msg[TBL_MSGBUFSZ])
{
  unsigned long key;

  if (parse_key (msg + sizeof "up", &key) < 0)
    return -1;

  table[key].state = 1;

  if (net_io_sendln (connfd, "ok", sizeof "ok") < 0)
    return -1;

  return 0;
}

static int
down_handler (int connfd, char msg[TBL_MSGBUFSZ])
{
  unsigned long key;

  if (parse_key (msg + sizeof "down", &key) < 0)
    return -1;

  table[key].state = 0;

  if (net_io_sendln (connfd, "ok", sizeof "ok") < 0)
    return -1;

  return 0;
}

static int
grab_handler (int connfd, char msg[TBL_MSGBUFSZ])
{
  unsigned long i, j, n, key, conns = 0;
  char *p;

  p = msg + sizeof "grab";

  pthread_mutex_lock (&lock);
  n = count;
  key = n;

  if (!strcmp (p, "lc"))
    {
      for (i = 0; i < n; ++i)
        if (table[i].state && (key == n || table[i].conns < table[key].conns))
          key = i;
    }

  else if (!strcmp (p, "rr"))
    {
      for (j = 0; j < n; ++j)
        {
          i = (rr + j) % n;

          if (table[i].state)
            {
              key = i;
              rr = i + 1;
              break;
            }
        }
    }

  else
    {
      pthread_mutex_unlock (&lock);
      return -1;
    }

  if (key < n)
    {
      conns = ++table[key].conns;
      ++total;
    }

  pthread_mutex_unlock (&lock);

  if (key == n)
    snprintf (msg, TBL_MSGBUFSZ, "none");

  else
    snprintf (msg, TBL_MSGBUFSZ, "%lu %s %s up %lu", key, table[key].ip,
              table[key].port, conns);

  if (net_io_sendln (connfd, msg, TBL_MSGBUFSZ) < 0)
    return -1;

  return 0;
}

static int
release_handler (int connfd, char msg[TBL_MSGBUFSZ])
{
  unsigned long key;

  if (parse_key (msg + sizeof "release", &key) < 0)
    return -1;

  pthread_mutex_lock (&lock);

  if (table[key].conns == 0)
    {
      pthread_mutex_unlock (&lock);
      return -1;
    }

  --table[key].conns;
  --total;

  pthread_mutex_unlock (&lock);

  if (net_io_sendln (connfd, "ok", sizeof "ok") < 0)
    return -1;

  return 0;
}

static int
status_handler (int connfd, char msg[TBL_MSGBUFSZ])
{
  unsigned long key;

  if (parse_key (msg + sizeof "status", &key) < 0)
    return -1;

  snprintf (msg, TBL_MSGBUFSZ, "%s %s %s %lu", table[key].ip, table[key].port,
            table[key].state ? "up" : "down", (unsigned long)table[key].conns);

  if (net_io_sendln (connfd, msg, TBL_MSGBUFSZ) < 0)
    return -1;

  return 0;
}

static int
total_handler (int connfd, char msg[TBL_MSGBUFSZ])
{
  snprintf (msg, TBL_MSGBUFSZ, "%lu", (unsigned long)total);

  if (net_io_sendln (connfd, msg, TBL_MSGBUFSZ) < 0)
    return -1;

  return 0;
}

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: table <ip>\n");
  exit (2);
}

static void
handle (int connfd)
{
  int ret;
  char msg[TBL_MSGBUFSZ];

  for (;;)
    {
      if (net_io_recvln (connfd, msg, sizeof msg) < 0)
        {
          /* too long  */
          if (errno == EMSGSIZE)
            continue;

          break;
        }

      /* handle command  */
      if (!strncmp (msg, "enroll ", sizeof "enroll"))
        ret = enroll_handler (connfd, msg);

      else if (!strncmp (msg, "up ", sizeof "up"))
        ret = up_handler (connfd, msg);

      else if (!strncmp (msg, "down ", sizeof "down"))
        ret = down_handler (connfd, msg);

      else if (!strncmp (msg, "grab ", sizeof "grab"))
        ret = grab_handler (connfd, msg);

      else if (!strncmp (msg, "release ", sizeof "release"))
        ret = release_handler (connfd, msg);

      else if (!strncmp (msg, "status ", sizeof "status"))
        ret = status_handler (connfd, msg);

      else if (!strcmp (msg, "total"))
        ret = total_handler (connfd, msg);

      else
        break;

      if (ret)
        break;
    }

  close (connfd);
}

int
main (int argc, char *argv[])
{
  const char *ip;
  int listenfd;

  if (argc != 2)
    usage ();

  ip = argv[1];

  if ((listenfd = net_tcp_listen (ip, TBL_PORT, INT_MAX)) < 0)
    pr_err (1, "net_tcp_listen: can't listen on %s:%s", ip, TBL_PORT);

  pr_debugx ("listening on %s:%s", ip, TBL_PORT);

  for (;;)
    if (net_tcp_serve (listenfd, handle) < 0)
      pr_warn ("net_tcp_serve");
}
