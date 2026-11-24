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
#include <ping/protocol.h>

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: pong [host] <port>\n");
  exit (2);
}

static void *
serve (void *arg)
{
  int cfd = (int)(intptr_t)arg;
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
  return NULL;
}

int
main (int argc, char *argv[])
{
  const char *host, *port;
  int lfd, cfd, rc;
  pthread_t t;

  if (argc != 2 && argc != 3)
    usage ();

  if (argc == 3)
    host = argv[1];

  else
    host = "0.0.0.0";

  port = argv[argc - 1];

  if ((lfd = net_tcp_listen (host, port, INT_MAX)) < 0)
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
