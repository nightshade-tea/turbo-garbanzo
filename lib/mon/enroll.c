#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <net/io.h>
#include <net/tcp.h>
#include <mon/protocol.h>

struct watch_ctx
{
  void (*lostfn) (void);
  int monfd;
};

static void *
watch (void *arg)
{
  struct watch_ctx w = *(struct watch_ctx *)arg;

  free (arg);

  /* mon never sends, so this only returns when the connection drops  */
  if (net_io_hold (w.monfd) == 0)
    errno = ENOTCONN;

  close (w.monfd);
  w.lostfn ();

  return NULL;
}

int
mon_enroll (const char *monhost, const char *ip, const char *port,
            void (*lostfn) (void))
{
  char msg[MON_MSGBUFSZ];
  struct watch_ctx *w;
  int monfd, rc;
  pthread_t t;

  if ((monfd = net_tcp_connect (monhost, MON_PORT)) < 0)
    return -1;

  snprintf (msg, sizeof msg, "%s %s", ip, port);

  if (net_io_sendln (monfd, msg, sizeof msg) < 0)
    goto err_net_io_sendln;

  if ((w = malloc (sizeof *w)) == NULL)
    goto err_malloc;

  w->lostfn = lostfn;
  w->monfd = monfd;

  if ((rc = pthread_create (&t, NULL, watch, w)))
    {
      errno = rc;
      goto err_pthread_create;
    }

  pthread_detach (t);
  return 0;

err_pthread_create:
  free (w);
err_malloc:
err_net_io_sendln:
  close (monfd);
  return -1;
}
