/**
 * src/loadb.c
 *
 * the load balancer listens on LOADB_PORT and relays each client connection
 * to a server picked by table, using the policy given on the command line:
 * lc (least connections) or rr (round robin).
 *
 * for each client, loadb grabs a server from table and connects to it. if the
 * connect fails, the server is released and another one is grabbed, until
 * table has no server up.
 *
 * then it relays one ping line to the server and one pong line back at a
 * time, until the client closes, and releases the server. if the server drops
 * before replying, loadb releases it, grabs another one and resends the ping,
 * so the client keeps going on a new server without noticing.
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
#include <ping/protocol.h>
#include <tbl/protocol.h>

static const char *policy, *tblhost;

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: loadb <ip> <lc|rr> <tblhost>\n");
  exit (2);
}

static int
grab (int tblfd, unsigned long *key)
{
  char ip[TBL_ADDRSTRLEN], port[TBL_PORTSTRLEN];
  int servfd, rc;

  for (;;)
    {
      if ((rc = tbl_grab (tblfd, policy, key, ip, port)) < 0)
        pr_err (1, "tbl_grab: grab failed");

      if (rc == 1)
        return -1;

      if ((servfd = net_tcp_connect (ip, port)) >= 0)
        {
          pr_debugx ("client on %s:%s (key %lu)", ip, port, *key);
          return servfd;
        }

      if (tbl_release (tblfd, *key) < 0)
        pr_err (1, "tbl_release: release failed (key %lu)", *key);
    }
}

/* byte relay, works for any protocol, but if the server dies the client
   dies with it.  */
#if 0
static void
relay (int connfd)
{
  unsigned long key;
  int tblfd, servfd;

  if ((tblfd = net_tcp_connect (tblhost, TBL_PORT)) < 0)
    pr_err (1, "net_tcp_connect: can't reach table (%s)", tblhost);

  if ((servfd = grab (tblfd, &key)) < 0)
    {
      pr_warnx ("no server up");
      goto err_grab;
    }

  if (net_io_relay (connfd, servfd) < 0)
    pr_warn ("net_io_relay");

  close (servfd);

  if (tbl_release (tblfd, key) < 0)
    pr_err (1, "tbl_release: release failed (key %lu)", key);

err_grab:
  close (tblfd);
  close (connfd);
}
#endif

/* ping pong specific relay function, which can move the client to a new
   server if the current one dies. this is only possible if loadb knows the
   protocol, because it would need to replay the message sent by the client to
   the new server. it is also only possible if the servers dont keep
   state/session, so they are interchangeable mid run (like pure functions,
   same input, same output).  */
static void
relay (int connfd)
{
  char msg[PING_MSGBUFSZ], buf[PING_MSGBUFSZ];
  unsigned long key;
  int tblfd, servfd;

  if ((tblfd = net_tcp_connect (tblhost, TBL_PORT)) < 0)
    pr_err (1, "net_tcp_connect: can't reach table (%s)", tblhost);

  if ((servfd = grab (tblfd, &key)) < 0)
    goto err_grab;

  while (net_io_recvln (connfd, msg, sizeof msg) >= 0
         && strcmp (msg, PING) == 0)
    {
      while (net_io_sendln (servfd, msg, sizeof msg) < 0
             || net_io_recvln (servfd, buf, sizeof buf) < 0)
        {
          pr_warn ("lost server (key %lu), failing over", key);
          close (servfd);

          if (tbl_release (tblfd, key) < 0)
            pr_err (1, "tbl_release: release failed (key %lu)", key);

          if ((servfd = grab (tblfd, &key)) < 0)
            goto err_grab;
        }

      if (net_io_sendln (connfd, buf, sizeof buf) < 0)
        break;
    }

  close (servfd);

  if (tbl_release (tblfd, key) < 0)
    pr_err (1, "tbl_release: release failed (key %lu)", key);

  close (tblfd);
  close (connfd);
  return;

err_grab:
  pr_warnx ("no server up");
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
