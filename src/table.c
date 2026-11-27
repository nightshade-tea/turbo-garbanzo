/**
 * src/table.c
 *
 * table holds the state of every enrolled server: its ip:port, whether it's
 * up or down, and how many connections it has. monitor uses it to enroll
 * servers and set their state, and loadb uses it to pick a server for each
 * client.
 *
 * table clients connect on TBL_PORT and send one command per line, getting
 * one reply line back, except for list and help, which reply with one line
 * per entry. a malformed command drops the connection.
 *
 *   enroll <ip> <port>  ->  <key>
 *   up <key>            ->  ok
 *   down <key>          ->  ok
 *   grab <lc|rr>        ->  <key> <ip> <port> | none
 *   release <key>       ->  ok
 *   status <key>        ->  <ip> <port> <up|down> <conns>
 *   total               ->  <total>
 *   list [up|down]      ->  <key> <ip> <port> <up|down> <conns> ... <n>
 *   help                ->  <command> ...
 *
 * grab picks an up server by policy and adds a connection to it, release
 * removes one. list ends with the number of entries listed. keys index the
 * table and are never reused, so a server that restarts gets its old key back.
 *
 * monitor --- enroll, up/down ---> table <--- grab/release --- loadb
 */

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
#include <tbl/wire.h>

#define TBL_HELP_ENROLL TBL_CMD_ENROLL " <ip> <port>"
#define TBL_HELP_UP TBL_CMD_UP " <key>"
#define TBL_HELP_DOWN TBL_CMD_DOWN " <key>"
#define TBL_HELP_GRAB TBL_CMD_GRAB " <" TBL_POLICY_LC "|" TBL_POLICY_RR ">"
#define TBL_HELP_RELEASE TBL_CMD_RELEASE " <key>"
#define TBL_HELP_STATUS TBL_CMD_STATUS " <key>"
#define TBL_HELP_TOTAL TBL_CMD_TOTAL
#define TBL_HELP_LIST TBL_CMD_LIST " [" TBL_UP "|" TBL_DOWN "]"
#define TBL_HELP_HELP TBL_CMD_HELP

static const char *const help[] = {
  TBL_HELP_ENROLL, TBL_HELP_UP,      TBL_HELP_DOWN,
  TBL_HELP_GRAB,   TBL_HELP_RELEASE, TBL_HELP_STATUS,
  TBL_HELP_TOTAL,  TBL_HELP_LIST,    TBL_HELP_HELP,
};

static struct entry
{
  char ip[TBL_ADDRSTRLEN];
  char port[TBL_PORTSTRLEN];
  _Atomic unsigned char state; /* 0 down, 1 up  */
  _Atomic unsigned long conns;
} table[TBL_LENGTH];

/* the counters and each entry's state and conns are atomic so status, list
   and total can read them without the lock  */
static _Atomic unsigned long tablecnt, lbtot;

/* guards enroll, grab, release and rr  */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long rr;

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: table <ip>\n");
  exit (2);
}

static int
parse_key (const char *msg, size_t off, unsigned long *key)
{
  const char *p = msg + off;
  unsigned long n;
  char *end;

  errno = 0;
  n = strtoul (p, &end, 10);

  if (end == p || *end != '\0' || errno == ERANGE || n >= tablecnt)
    {
      pr_warnx ("bad key (%s)", msg);
      return -1;
    }

  *key = n;

  return 0;
}

static int
enroll_handler (char msg[TBL_MSGBUFSZ])
{
  char ip[TBL_ADDRSTRLEN], port[TBL_PORTSTRLEN], *p, *sp;
  size_t iplen, portlen;
  unsigned long key, n;

  p = msg + sizeof TBL_CMD_ENROLL;

  if ((sp = strchr (p, ' ')) == NULL)
    {
      pr_warnx ("ip:port pair is missing ' ' separator (%s)", msg);
      return -1;
    }

  iplen = (size_t)(sp - p);
  portlen = strlen (sp + 1);

  if (iplen == 0 || iplen >= sizeof ip || portlen == 0
      || portlen >= sizeof port)
    {
      pr_warnx ("bad ip:port pair (%s)", msg);
      return -1;
    }

  memcpy (ip, p, iplen);
  ip[iplen] = '\0';
  memcpy (port, sp + 1, portlen + 1);

  pthread_mutex_lock (&lock);
  n = tablecnt;

  for (key = 0; key < n; ++key)
    if (strcmp (table[key].ip, ip) == 0 && strcmp (table[key].port, port) == 0)
      break;

  if (key == n)
    {
      if (n == TBL_LENGTH)
        {
          pthread_mutex_unlock (&lock);
          pr_warnx ("table is full (%s:%s)", ip, port);
          return -1;
        }

      memcpy (table[key].ip, ip, sizeof ip);
      memcpy (table[key].port, port, sizeof port);

      /* publish only after the entry is filled  */
      tablecnt = n + 1;

      pr_debugx ("%s:%s (key %lu) enrolled", ip, port, key);
    }

  pthread_mutex_unlock (&lock);

  snprintf (msg, TBL_MSGBUFSZ, "%lu", key);

  return 0;
}

static int
up_handler (char msg[TBL_MSGBUFSZ])
{
  unsigned long key;

  if (parse_key (msg, sizeof TBL_CMD_UP, &key) < 0)
    return -1;

  table[key].state = 1;
  pr_debugx ("%s:%s (key %lu) up", table[key].ip, table[key].port, key);

  snprintf (msg, TBL_MSGBUFSZ, TBL_OK);

  return 0;
}

static int
down_handler (char msg[TBL_MSGBUFSZ])
{
  unsigned long key;

  if (parse_key (msg, sizeof TBL_CMD_DOWN, &key) < 0)
    return -1;

  table[key].state = 0;
  pr_debugx ("%s:%s (key %lu) down", table[key].ip, table[key].port, key);

  snprintf (msg, TBL_MSGBUFSZ, TBL_OK);

  return 0;
}

static int
grab_handler (char msg[TBL_MSGBUFSZ])
{
  unsigned long i, j, n, key, conns = 0;
  char *p;

  p = msg + sizeof TBL_CMD_GRAB;

  pthread_mutex_lock (&lock);
  n = tablecnt;
  key = n;

  if (strcmp (p, TBL_POLICY_LC) == 0)
    {
      /* the up server with the fewest conns  */
      for (i = 0; i < n; ++i)
        if (table[i].state && (key == n || table[i].conns < table[key].conns))
          key = i;
    }

  else if (strcmp (p, TBL_POLICY_RR) == 0)
    {
      /* the next up server after the last pick, wrapping around  */
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
      pr_warnx ("bad policy (%s)", msg);
      return -1;
    }

  if (key < n)
    {
      conns = ++table[key].conns;
      ++lbtot;
    }

  pthread_mutex_unlock (&lock);

  if (key == n)
    {
      pr_debugx ("no server up (%s)", p);
      snprintf (msg, TBL_MSGBUFSZ, TBL_NONE);
      return 0;
    }

  pr_debugx ("%s:%s (key %lu) grabbed, %lu conns", table[key].ip,
             table[key].port, key, conns);

  snprintf (msg, TBL_MSGBUFSZ, "%lu %s %s", key, table[key].ip,
            table[key].port);

  return 0;
}

static int
release_handler (char msg[TBL_MSGBUFSZ])
{
  unsigned long key, conns;

  if (parse_key (msg, sizeof TBL_CMD_RELEASE, &key) < 0)
    return -1;

  pthread_mutex_lock (&lock);

  if (table[key].conns == 0)
    {
      pthread_mutex_unlock (&lock);
      pr_warnx ("%s:%s (key %lu) has no conns to release", table[key].ip,
                table[key].port, key);
      return -1;
    }

  conns = --table[key].conns;
  --lbtot;

  pthread_mutex_unlock (&lock);

  pr_debugx ("%s:%s (key %lu) released, %lu conns", table[key].ip,
             table[key].port, key, conns);

  snprintf (msg, TBL_MSGBUFSZ, TBL_OK);

  return 0;
}

static int
status_handler (char msg[TBL_MSGBUFSZ])
{
  unsigned long key;

  if (parse_key (msg, sizeof TBL_CMD_STATUS, &key) < 0)
    return -1;

  snprintf (msg, TBL_MSGBUFSZ, "%s %s %s %lu", table[key].ip, table[key].port,
            table[key].state ? TBL_UP : TBL_DOWN,
            (unsigned long)table[key].conns);

  return 0;
}

static int
total_handler (char msg[TBL_MSGBUFSZ])
{
  snprintf (msg, TBL_MSGBUFSZ, "%lu", (unsigned long)lbtot);

  return 0;
}

static int
list_handler (int connfd, char msg[TBL_MSGBUFSZ])
{
  unsigned long key, n, listed = 0;
  char line[TBL_MSGBUFSZ], *p;
  int filter = -1, state;

  p = msg + sizeof TBL_CMD_LIST - 1;

  /* optional up|down filter  */
  if (*p == ' ')
    {
      if (strcmp (p + 1, TBL_UP) == 0)
        filter = 1;

      else if (strcmp (p + 1, TBL_DOWN) == 0)
        filter = 0;

      else
        {
          pr_warnx ("bad filter (%s)", msg);
          return -1;
        }
    }

  n = tablecnt;

  for (key = 0; key < n; ++key)
    {
      state = table[key].state;

      if (filter >= 0 && state != filter)
        continue;

      snprintf (line, sizeof line, "%lu %s %s %s %lu", key, table[key].ip,
                table[key].port, state ? TBL_UP : TBL_DOWN,
                (unsigned long)table[key].conns);

      if (net_io_sendln (connfd, line, sizeof line) < 0)
        {
          pr_warn ("net_io_sendln");
          return -1;
        }

      ++listed;
    }

  snprintf (msg, TBL_MSGBUFSZ, "%lu", listed);

  return 0;
}

static int
help_handler (int connfd, char msg[TBL_MSGBUFSZ])
{
  size_t i, n = sizeof help / sizeof *help;

  for (i = 0; i < n - 1; ++i)
    if (net_io_sendln (connfd, help[i], strlen (help[i])) < 0)
      {
        pr_warn ("net_io_sendln");
        return -1;
      }

  /* last line goes out with the rest of the replies  */
  snprintf (msg, TBL_MSGBUFSZ, "%s", help[n - 1]);

  return 0;
}

static void
handle (int connfd)
{
  char msg[TBL_MSGBUFSZ];
  int ret;

  for (;;)
    {
      if (net_io_recvln (connfd, msg, sizeof msg) < 0)
        {
          if (errno == EMSGSIZE)
            pr_warnx ("line too long (%s...)", msg);

          else if (errno != ENOTCONN)
            pr_warn ("net_io_recvln");

          break;
        }

      if (strncmp (msg, TBL_CMD_ENROLL " ", sizeof TBL_CMD_ENROLL) == 0)
        ret = enroll_handler (msg);

      else if (strncmp (msg, TBL_CMD_UP " ", sizeof TBL_CMD_UP) == 0)
        ret = up_handler (msg);

      else if (strncmp (msg, TBL_CMD_DOWN " ", sizeof TBL_CMD_DOWN) == 0)
        ret = down_handler (msg);

      else if (strncmp (msg, TBL_CMD_GRAB " ", sizeof TBL_CMD_GRAB) == 0)
        ret = grab_handler (msg);

      else if (strncmp (msg, TBL_CMD_RELEASE " ", sizeof TBL_CMD_RELEASE) == 0)
        ret = release_handler (msg);

      else if (strncmp (msg, TBL_CMD_STATUS " ", sizeof TBL_CMD_STATUS) == 0)
        ret = status_handler (msg);

      else if (strcmp (msg, TBL_CMD_TOTAL) == 0)
        ret = total_handler (msg);

      else if (strcmp (msg, TBL_CMD_LIST) == 0
               || strncmp (msg, TBL_CMD_LIST " ", sizeof TBL_CMD_LIST) == 0)
        ret = list_handler (connfd, msg);

      else if (strcmp (msg, TBL_CMD_HELP) == 0)
        ret = help_handler (connfd, msg);

      else
        {
          pr_warnx ("unknown command (%s)", msg);
          break;
        }

      if (ret < 0)
        break;

      if (net_io_sendln (connfd, msg, sizeof msg) < 0)
        {
          pr_warn ("net_io_sendln");
          break;
        }
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
