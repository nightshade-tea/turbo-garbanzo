#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <err.h>

#include <net/io.h>
#include <net/tcp.h>
#include <monitor/protocol.h>
#include <table/protocol.h>

#ifndef TCP_KEEPIDLE
#define TCP_KEEPIDLE TCP_KEEPALIVE
#endif

struct slot
{
  pthread_mutex_t mutex;
  int fd;
};

static const char *tblhost;

/* held by a thread for as long as it watches that key  */
static struct slot slots[TABLE_LENGTH];

/* guards slots[].fd  */
static pthread_mutex_t slots_lock = PTHREAD_MUTEX_INITIALIZER;

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: monitor <ip> <tblsrv>\n");
  exit (2);
}

static void *
serve (void *arg)
{
  int sfd = (int)(intptr_t)arg, tfd, one = 1, idle = 2, intvl = 1, cnt = 2;
  char msg[64], *ip, *port, c;
  unsigned long key;
  ssize_t n;

  if (net_io_recvln (sfd, msg, sizeof msg) < 0)
    goto err_net_io_recvln;

  ip = msg;

  if ((port = strchr (msg, ' ')) == NULL)
    goto err_strchr;

  *port++ = '\0';

  /* tblsrv would close on these, which would kill us  */
  if (strlen (ip) >= TABLE_ADDRSTRLEN || strlen (port) >= TABLE_PORTSTRLEN)
    goto err_strlen;

  if (setsockopt (sfd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one)
      || setsockopt (sfd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle)
      || setsockopt (sfd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof intvl)
      || setsockopt (sfd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof cnt))
    err (1, "setsockopt");

  if ((tfd = net_tcp_connect (tblhost, TABLE_PORT)) < 0)
    exit (1);

  if (table_protocol_enroll (tfd, ip, port, &key) < 0)
    errx (1, "enroll failed");

  /* take over the slot, waking up whoever was watching it  */
  pthread_mutex_lock (&slots_lock);

  if (slots[key].fd >= 0)
    shutdown (slots[key].fd, SHUT_RDWR);

  slots[key].fd = sfd;
  pthread_mutex_unlock (&slots_lock);

  /* wait for it to mark the key down  */
  pthread_mutex_lock (&slots[key].mutex);

  if (table_protocol_up (tfd, key) < 0)
    errx (1, "up failed");

  /* pong never sends, so this only returns when the connection dies  */
  while ((n = recv (sfd, &c, 1, 0)) > 0 || (n < 0 && errno == EINTR))
    ;

  if (table_protocol_down (tfd, key) < 0)
    errx (1, "down failed");

  /* clear before close, so shutdown never hits a reused fd  */
  pthread_mutex_lock (&slots_lock);

  if (slots[key].fd == sfd)
    slots[key].fd = -1;

  pthread_mutex_unlock (&slots_lock);

  close (sfd);
  pthread_mutex_unlock (&slots[key].mutex);
  close (tfd);

  return NULL;

err_strlen:
err_strchr:
err_net_io_recvln:
  close (sfd);
  return NULL;
}

int
main (int argc, char *argv[])
{
  int lfd, cfd, rc;
  pthread_t t;
  size_t i;

  if (argc != 3)
    usage ();

  tblhost = argv[2];

  for (i = 0; i < TABLE_LENGTH; ++i)
    {
      pthread_mutex_init (&slots[i].mutex, NULL);
      slots[i].fd = -1;
    }

  if ((lfd = net_tcp_listen (argv[1], MONITOR_PORT, INT_MAX)) < 0)
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
