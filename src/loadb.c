/**
 * src/loadb.c
 *
 * the load balancer listens on LOADB_PORT and relays each client connection
 * to a server picked by table, using the policy given on the command line:
 * lc (least connections) or rr (round robin).
 *
 * for each client, loadb grabs a server from table, connects to it and relays
 * data both ways until both sides close, then releases it. if the connect
 * fails, the server is released and another one is grabbed, until table has
 * no server up.
 *
 * client <--> loadb <--> server
 *               |
 *     grab/release key
 *               |
 *             table
 */

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
  int tblfd, servfd, rc;
  char ip[TBL_ADDRSTRLEN], port[TBL_PORTSTRLEN];
  unsigned long key;

  if ((tblfd = net_tcp_connect (tblhost, TBL_PORT)) < 0)
    pr_err (1, "net_tcp_connect: can't reach table (%s)", tblhost);

  for (;;)
    {
      if ((rc = tbl_grab (tblfd, policy, &key, ip, port)) < 0)
        pr_err (1, "tbl_grab: grab failed");

      if (rc == 1)
        {
          pr_warnx ("no server up");
          goto err_tbl_grab;
        }

      if ((servfd = net_tcp_connect (ip, port)) >= 0)
        break;

      if (tbl_release (tblfd, key) < 0)
        pr_err (1, "tbl_release: release failed (key %lu)", key);
    }

  if (net_io_relay (connfd, servfd) < 0)
    pr_warn ("net_io_relay");

  close (servfd);

  if (tbl_release (tblfd, key) < 0)
    pr_err (1, "tbl_release: release failed (key %lu)", key);

err_tbl_grab:
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

  if (strcmp (policy, TBL_POLICY_LC) != 0
      && strcmp (policy, TBL_POLICY_RR) != 0)
    usage ();

  if ((listenfd = net_tcp_listen (ip, LOADB_PORT, INT_MAX)) < 0)
    pr_err (1, "net_tcp_listen: can't listen on %s:%s", ip, LOADB_PORT);

  pr_debugx ("listening on %s:%s", ip, LOADB_PORT);

  for (;;)
    if (net_tcp_serve (listenfd, relay) < 0)
      pr_warn ("net_tcp_serve");
}
