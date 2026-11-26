#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <log/pr.h>
#include <net/io.h>
#include <net/tcp.h>
#include <ping/protocol.h>
#include <mon/protocol.h>

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: pong <ip> <port> <monitor>\n");
  exit (2);
}

[[noreturn]]
static void
lost (void)
{
  pr_err (1, "lost connection to monitor");
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
  int lfd;

  if (argc != 4)
    usage ();

  if ((lfd = net_tcp_listen (argv[1], argv[2], INT_MAX)) < 0)
    pr_err (1, "net_tcp_listen: can't listen on %s:%s", argv[1], argv[2]);

  if (mon_enroll (argv[3], argv[1], argv[2], lost) < 0)
    pr_err (1, "mon_enroll: can't reach monitor (%s)", argv[3]);

  for (;;)
    if (net_tcp_serve (lfd, serve) < 0)
      pr_warn ("net_tcp_serve");
}
