#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

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
  fprintf (stderr, "usage: loadb <ip> <lc|rr> <tblsrv>\n");
  exit (2);
}

static void
serve (int cfd)
{
  int tfd, sfd = -1, i, rc;
  struct server srv;
  unsigned long key;

  if ((tfd = net_tcp_connect (tblhost, TBL_PORT)) < 0)
    exit (1);

  for (i = 0; i < CONNECT_TRIES; ++i)
    {
      if ((rc = tbl_grab (tfd, policy, &key, &srv)) < 0)
        errx (1, "grab failed");

      /* no server up  */
      if (rc == 1)
        break;

      if ((sfd = net_tcp_connect (srv.ip, srv.port)) >= 0)
        break;

      if (tbl_release (tfd, key) < 0)
        errx (1, "release failed");
    }

  if (sfd < 0)
    goto err_net_tcp_connect;

  if (net_io_relay (cfd, sfd) < 0)
    warn ("net_io_relay");

  close (sfd);

  if (tbl_release (tfd, key) < 0)
    errx (1, "release failed");

err_net_tcp_connect:
  close (tfd);
  close (cfd);
}

int
main (int argc, char *argv[])
{
  int lfd;

  if (argc != 4)
    usage ();

  policy = argv[2];
  tblhost = argv[3];

  if (strcmp (policy, "lc") && strcmp (policy, "rr"))
    usage ();

  if ((lfd = net_tcp_listen (argv[1], LOADB_PORT, INT_MAX)) < 0)
    return 1;

  for (;;)
    if (net_tcp_serve (lfd, serve) < 0)
      warn ("net_tcp_serve");
}
