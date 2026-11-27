/**
 * src/pong.c
 *
 * pong is the server behind the load balancer. it listens on ip:port,
 * enrolls itself on monitor, and replies pong to every ping it receives.
 *
 * it stays enrolled for as long as the connection to monitor lives. if that
 * connection drops, pong exits, since monitor can no longer watch it and
 * table would have stale state.
 *
 * pong --- ip:port ---> monitor
 * ping --- ping ---> pong
 *      <-- pong ---
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <log/pr.h>
#include <net/io.h>
#include <net/tcp.h>
#include <mon/protocol.h>
#include <ping/protocol.h>

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: pong <ip> <port> <monhost>\n");
  exit (2);
}

[[noreturn]]
static void
lost (void)
{
  pr_err (1, "lost connection to monitor");
}

static void
reply (int connfd)
{
  char msg[PING_MSGBUFSZ];

  for (;;)
    {
      if (net_io_recvln (connfd, msg, sizeof msg) < 0)
        break;

      if (strcmp (msg, PING) != 0
          || net_io_sendln (connfd, PONG, sizeof PONG) < 0)
        break;
    }

  close (connfd);
}

int
main (int argc, char *argv[])
{
  const char *ip, *port, *monhost;
  int listenfd;

  if (argc != 4)
    usage ();

  ip = argv[1];
  port = argv[2];
  monhost = argv[3];

  if ((listenfd = net_tcp_listen (ip, port, INT_MAX)) < 0)
    pr_err (1, "net_tcp_listen: can't listen on %s:%s", ip, port);

  pr_debugx ("listening on %s:%s", ip, port);

  if (mon_enroll (monhost, ip, port, lost) < 0)
    pr_err (1, "mon_enroll: can't reach monitor (%s)", monhost);

  for (;;)
    if (net_tcp_serve (listenfd, reply) < 0)
      pr_warn ("net_tcp_serve");
}
