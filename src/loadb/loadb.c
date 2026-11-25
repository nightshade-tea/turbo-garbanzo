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
#include <loadb/protocol.h>
#include <table/protocol.h>

#define CONNECT_TRIES 3

struct relay
{
  int from, to;
};

static const char *policy, *tblhost;

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: loadb <ip> <lc|rr> <tblsrv>\n");
  exit (2);
}

static void *
relay (void *arg)
{
  struct relay *r = arg;
  char buf[4096];
  ssize_t n;

  for (;;)
    {
      if ((n = recv (r->from, buf, sizeof buf, 0)) < 0)
        {
          if (errno == EINTR)
            continue;

          break;
        }

      if (n == 0 || net_io_send (r->to, buf, (size_t)n) < 0)
        break;
    }

  /* pass the eof on, so the other direction ends too  */
  shutdown (r->to, SHUT_WR);

  return NULL;
}

static void *
serve (void *arg)
{
  int cfd = (int)(intptr_t)arg, tfd, sfd = -1, i, rc;
  struct relay fwd, back;
  struct server srv;
  unsigned long key;
  pthread_t t;

  if ((tfd = net_tcp_connect (tblhost, TABLE_PORT)) < 0)
    exit (1);

  for (i = 0; i < CONNECT_TRIES; ++i)
    {
      if ((rc = table_protocol_grab (tfd, policy, &key, &srv)) < 0)
        errx (1, "grab failed");

      /* no server up  */
      if (rc == 1)
        break;

      if ((sfd = net_tcp_connect (srv.ip, srv.port)) >= 0)
        break;

      if (table_protocol_release (tfd, key) < 0)
        errx (1, "release failed");
    }

  if (sfd < 0)
    goto err_net_tcp_connect;

  fwd = (struct relay){ .from = cfd, .to = sfd };
  back = (struct relay){ .from = sfd, .to = cfd };

  if ((rc = pthread_create (&t, NULL, relay, &back)))
    {
      errno = rc;
      warn ("pthread_create");
      goto err_pthread_create;
    }

  relay (&fwd);
  pthread_join (t, NULL);

err_pthread_create:
  close (sfd);

  if (table_protocol_release (tfd, key) < 0)
    errx (1, "release failed");

err_net_tcp_connect:
  close (tfd);
  close (cfd);

  return NULL;
}

int
main (int argc, char *argv[])
{
  int lfd, cfd, rc;
  pthread_t t;

  if (argc != 4)
    usage ();

  policy = argv[2];
  tblhost = argv[3];

  if (strcmp (policy, "lc") && strcmp (policy, "rr"))
    usage ();

  if ((lfd = net_tcp_listen (argv[1], LOADB_PORT, INT_MAX)) < 0)
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
