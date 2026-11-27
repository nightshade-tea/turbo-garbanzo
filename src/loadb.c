#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <log/pr.h>
#include <net/io.h>
#include <net/tcp.h>
#include <loadb/protocol.h>
#include <tbl/protocol.h>

#define CONNECT_TRIES 3

static const char *policy, *tblhost;

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: loadb <ip> <lc|rr> <tblhost>\n");
  exit (2);
}

static void
relay (int connfd)
{
  int tblfd, servfd = -1, i, rc;
  struct server srv;
  unsigned long key;

  if ((tblfd = net_tcp_connect (tblhost, TBL_PORT)) < 0)
    pr_err (1, "net_tcp_connect: can't reach table (%s)", tblhost);

  for (i = 0; i < CONNECT_TRIES; ++i)
    {
      if ((rc = tbl_grab (tblfd, policy, &key, &srv)) < 0)
        pr_err (1, "tbl_grab: grab failed");

      /* no server up  */
      if (rc == 1)
        break;

      if ((servfd = net_tcp_connect (srv.ip, srv.port)) >= 0)
        break;

      if (tbl_release (tblfd, key) < 0)
        pr_err (1, "tbl_release: release failed (key %lu)", key);
    }

  if (servfd < 0)
    goto err_net_tcp_connect;

  if (net_io_relay (connfd, servfd) < 0)
    pr_warn ("net_io_relay");

  close (servfd);

  if (tbl_release (tblfd, key) < 0)
    pr_err (1, "tbl_release: release failed (key %lu)", key);

err_net_tcp_connect:
  close (tblfd);
  close (connfd);
}

int
main (int argc, char *argv[])
{
  const char *ip;
  int listenfd;

  if (argc != 4)
    usage ();

  ip = argv[1];
  policy = argv[2];
  tblhost = argv[3];

  if (strcmp (policy, "lc") && strcmp (policy, "rr"))
    usage ();

  if ((listenfd = net_tcp_listen (ip, LOADB_PORT, INT_MAX)) < 0)
    pr_err (1, "net_tcp_listen: can't listen on %s:%s", ip, LOADB_PORT);

  pr_debugx ("listening on %s:%s", ip, LOADB_PORT);

  for (;;)
    if (net_tcp_serve (listenfd, relay) < 0)
      pr_warn ("net_tcp_serve");
}
