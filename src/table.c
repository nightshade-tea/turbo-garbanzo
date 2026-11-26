#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <err.h>

#include <net/io.h>
#include <net/tcp.h>
#include <table/protocol.h>

#define MSGBUFSZ 256

static struct server table[TABLE_LENGTH];
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
enroll_handler (int sock, char msg[MSGBUFSZ])
{
  char ip[TABLE_ADDRSTRLEN], port[TABLE_PORTSTRLEN], *p, *sp;
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
      if (n == TABLE_LENGTH)
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

  snprintf (msg, MSGBUFSZ, "%lu", key);

  if (net_io_sendln (sock, msg, MSGBUFSZ) < 0)
    return -1;

  return 0;
}

static int
up_handler (int sock, char msg[MSGBUFSZ])
{
  unsigned long key;

  if (parse_key (msg + sizeof "up", &key) < 0)
    return -1;

  table[key].state = 1;

  if (net_io_sendln (sock, "ok", sizeof "ok") < 0)
    return -1;

  return 0;
}

static int
down_handler (int sock, char msg[MSGBUFSZ])
{
  unsigned long key;

  if (parse_key (msg + sizeof "down", &key) < 0)
    return -1;

  table[key].state = 0;

  if (net_io_sendln (sock, "ok", sizeof "ok") < 0)
    return -1;

  return 0;
}

static int
grab_handler (int sock, char msg[MSGBUFSZ])
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
    snprintf (msg, MSGBUFSZ, "none");

  else
    snprintf (msg, MSGBUFSZ, "%lu %s %s up %lu", key, table[key].ip,
              table[key].port, conns);

  if (net_io_sendln (sock, msg, MSGBUFSZ) < 0)
    return -1;

  return 0;
}

static int
release_handler (int sock, char msg[MSGBUFSZ])
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

  if (net_io_sendln (sock, "ok", sizeof "ok") < 0)
    return -1;

  return 0;
}

static int
status_handler (int sock, char msg[MSGBUFSZ])
{
  unsigned long key;

  if (parse_key (msg + sizeof "status", &key) < 0)
    return -1;

  snprintf (msg, MSGBUFSZ, "%s %s %s %lu", table[key].ip, table[key].port,
            table[key].state ? "up" : "down", (unsigned long)table[key].conns);

  if (net_io_sendln (sock, msg, MSGBUFSZ) < 0)
    return -1;

  return 0;
}

static int
total_handler (int sock, char msg[MSGBUFSZ])
{
  snprintf (msg, MSGBUFSZ, "%lu", (unsigned long)total);

  if (net_io_sendln (sock, msg, MSGBUFSZ) < 0)
    return -1;

  return 0;
}

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: table [host]\n");
  exit (2);
}

static void
serve (int cfd)
{
  int ret;
  char msg[MSGBUFSZ];

  for (;;)
    {
      if (net_io_recvln (cfd, msg, sizeof msg) < 0)
        {
          /* too long  */
          if (errno == EMSGSIZE)
            continue;

          break;
        }

      /* handle command  */
      if (!strncmp (msg, "enroll ", sizeof "enroll"))
        ret = enroll_handler (cfd, msg);

      else if (!strncmp (msg, "up ", sizeof "up"))
        ret = up_handler (cfd, msg);

      else if (!strncmp (msg, "down ", sizeof "down"))
        ret = down_handler (cfd, msg);

      else if (!strncmp (msg, "grab ", sizeof "grab"))
        ret = grab_handler (cfd, msg);

      else if (!strncmp (msg, "release ", sizeof "release"))
        ret = release_handler (cfd, msg);

      else if (!strncmp (msg, "status ", sizeof "status"))
        ret = status_handler (cfd, msg);

      else if (!strcmp (msg, "total"))
        ret = total_handler (cfd, msg);

      else
        break;

      if (ret)
        break;
    }

  close (cfd);
}

int
main (int argc, char *argv[])
{
  const char *host;
  int lfd;

  if (argc != 1 && argc != 2)
    usage ();

  if (argc == 2)
    host = argv[1];

  else
    host = "0.0.0.0";

  if ((lfd = net_tcp_listen (host, TABLE_PORT, INT_MAX)) < 0)
    return 1;

  for (;;)
    if (net_tcp_serve (lfd, serve) < 0)
      warn ("net_tcp_serve");
}
