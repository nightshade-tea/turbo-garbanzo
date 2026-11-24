#include <errno.h>
#include <limits.h>
#include <stdint.h>
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

#define GET "get"
#define SET "set"
#define INC "inc"
#define DEC "dec"

/* no locks because each monitor thread controls a single key (too tired)  */
static unsigned long table[TABLE_LENGTH] = { 0 };

static int
get_handler (int sock, char msg[MSGBUFSZ])
{
  unsigned long key;
  char *p, *end;

  p = msg + sizeof GET;

  errno = 0;
  key = strtoul (p, &end, 10);

  if (*end != '\0' || errno == ERANGE || key >= TABLE_LENGTH)
    return -1;

  snprintf (msg, MSGBUFSZ, "%lu", table[key]);

  if (net_io_sendln (sock, msg, MSGBUFSZ) < 0)
    return -1;

  return 0;
}

static int
set_handler (char msg[MSGBUFSZ])
{
  unsigned long key, val;
  char *p, *end;

  p = msg + sizeof SET;

  errno = 0;
  key = strtoul (p, &end, 10);

  if (*end != ' ' || errno == ERANGE || key >= TABLE_LENGTH)
    return -1;

  p = end + 1;
  val = strtoul (p, &end, 10);

  if (*end != '\0' || errno == ERANGE)
    return -1;

  table[key] = val;

  return 0;
}

static int
inc_handler (char msg[MSGBUFSZ])
{
  unsigned long key;
  char *p, *end;

  p = msg + sizeof INC;

  errno = 0;
  key = strtoul (p, &end, 10);

  if (*end != '\0' || errno == ERANGE || key >= TABLE_LENGTH)
    return -1;

  ++table[key];

  return 0;
}

static int
dec_handler (char msg[MSGBUFSZ])
{
  unsigned long key;
  char *p, *end;

  p = msg + sizeof DEC;

  errno = 0;
  key = strtoul (p, &end, 10);

  if (*end != '\0' || errno == ERANGE || key >= TABLE_LENGTH)
    return -1;

  --table[key];

  return 0;
}

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: tblsrv [host]\n");
  exit (2);
}

static void *
serve (void *arg)
{
  int ret, cfd = (int)(intptr_t)arg;
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
      if (!strncmp (msg, GET " ", sizeof GET))
        ret = get_handler (cfd, msg);

      else if (!strncmp (msg, SET " ", sizeof SET))
        ret = set_handler (msg);

      else if (!strncmp (msg, INC " ", sizeof INC))
        ret = inc_handler (msg);

      else if (!strncmp (msg, DEC " ", sizeof DEC))
        ret = dec_handler (msg);

      else
        break;

      if (ret)
        break;
    }

  close (cfd);
  return NULL;
}

int
main (int argc, char *argv[])
{
  const char *host;
  int lfd, cfd, rc;
  pthread_t t;

  if (argc != 1 && argc != 2)
    usage ();

  if (argc == 2)
    host = argv[1];

  else
    host = "0.0.0.0";

  if ((lfd = net_tcp_listen (host, TABLE_PORT, INT_MAX)) < 0)
    return 1;

  for (;;)
    {
      if ((cfd = accept (lfd, NULL, NULL)) < 0)
        {
          warn ("accept");
          continue;
        }

      if ((rc = pthread_create (&t, NULL, serve, (void *)(intptr_t)cfd)))
        {
          errno = rc;
          warn ("pthread_create");
          close (cfd);
          continue;
        }

      pthread_detach (t);
    }
}
