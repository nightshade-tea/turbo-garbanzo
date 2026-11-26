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
#include <ping/protocol.h>
#include <monitor/protocol.h>

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: pong <ip> <port> <monitor>\n");
  exit (2);
}

[[noreturn]]
static void *
mon (void *arg)
{
  char **argv = arg, msg[64], c;
  int monfd;

  if ((monfd = net_tcp_connect (argv[3], MONITOR_PORT)) < 0)
    exit (1);

  snprintf (msg, sizeof msg, "%s %s", argv[1], argv[2]);

  if (net_io_sendln (monfd, msg, sizeof msg) < 0)
    err (1, "send");

  /* the monitor never sends, so returning means it's gone  */
  while (recv (monfd, &c, 1, 0) < 0 && errno == EINTR)
    ;

  errx (1, "monitor closed the connection");
}

static void
serve (int cfd)
{
  char line[64];

  for (;;)
    {
      if (net_io_recvln (cfd, line, sizeof line) < 0)
        {
          /* too long  */
          if (errno == EMSGSIZE)
            continue;

          break;
        }

      if (strcmp (line, PING) == 0
          && net_io_sendln (cfd, PONG, sizeof PONG) < 0)
        break;
    }

  close (cfd);
}

int
main (int argc, char *argv[])
{
  int lfd, rc;
  pthread_t t;

  if (argc != 4)
    usage ();

  if ((lfd = net_tcp_listen (argv[1], argv[2], INT_MAX)) < 0)
    return 1;

  if ((rc = pthread_create (&t, NULL, mon, argv)))
    {
      errno = rc;
      err (1, "pthread_create");
    }

  pthread_detach (t);

  for (;;)
    if (net_tcp_serve (lfd, serve) < 0)
      warn ("net_tcp_serve");
}
