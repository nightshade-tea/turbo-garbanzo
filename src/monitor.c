/**
 * src/monitor.c
 *
 * the monitor is responsible for enrolling and managing the state of the
 * servers in table, so whenever a server starts it has to set its state to
 * up, and whenever a server stops/crashes it has to set its state to down.
 *
 * to achieve this, the monitor listens on MONITOR_PORT and the servers connect
 * to it on startup. each server connection (sfd) gets a manager thread that
 * enrolls the server on table and checks that the server is alive by setting
 * SO_KEEPALIVE on sfd. if a server dies, the keepalive will fail and the
 * manager thread wakes up and uses its connection to table (tfd) to set that
 * server's state as down.
 *
 * server --- ip:port ---> monitor --- enroll ip:port, up/down key --> table
 *
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include <log/pr.h>
#include <net/io.h>
#include <net/tcp.h>
#include <monitor/protocol.h>
#include <table/protocol.h>

#define MSGBUFSZ 64 /* rw buf for sfd  */

/**
 * struct slot
 *
 * control structure to manage the servers
 *
 * suppose a server quickly restarts, and before the old manager thread notices
 * that the server died, that server reconnects to monitor and gets a new
 * manager thread.
 *
 * momentarily, we'd have two threads that are managing the same server, and
 * that could lead to problems: the old thread may set its state to down after
 * the new thread has set the state to up, and that server would be wrongfully
 * marked as down on table.
 *
 * because of that, we need to ensure that only the new thread sets the state
 * for that server in table and sets slot[key].sfd with the current
 * connection's socket file descriptor.
 *
 * to guarantee that, we make the manager threads grab slot[key].slock before
 * setting the server's state, and only do that when slot[key].sfd is the same
 * as its local sfd variable. that way, the new thread always sets the state
 * last and slot[key].sfd isn't clobbered.
 *
 * and having slot[key].sfd store the current manager thread's server
 * connection file descriptor allows the new thread to close the old thread's
 * connection, so it wakes up and dies.
 *
 */

static struct slot
{
  pthread_mutex_t slock; /* guards sfd and the key's up/down  */
  int sfd;               /* connection watching the key, or -1  */
} slots[TABLE_LENGTH];   /* one slot for each key  */

static const char *tblhost;

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: monitor <ip> <table>\n");
  exit (2);
}

static void
manage (int sfd)
{
  int tfd, one = 1, idle = 2, intvl = 1, cnt = 2;
  char msg[MSGBUFSZ], *ip, *port, c;
  unsigned long key;
  ssize_t n;

  /* server sends "<ip> <port>"  */
  if (net_io_recvln (sfd, msg, sizeof msg) < 0)
    {
      pr_warn ("net_io_recvln");
      goto err_net_io_recvln;
    }

  ip = msg;

  if ((port = strchr (msg, ' ')) == NULL)
    {
      pr_warnx ("ip:port pair is missing ' ' separator (%s)", msg);
      goto err_strchr;
    }

  /* split msg into "<ip>" "<port>"  */
  *port++ = '\0';

  if (strlen (ip) >= TABLE_ADDRSTRLEN || strlen (port) >= TABLE_PORTSTRLEN)
    {
      pr_warnx ("ip:port pair is too long (%s:%s)", ip, port);
      goto err_strlen;
    }

  /* 2s idle + 2 probes 1s apart  */
  if (setsockopt (sfd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one)
      || setsockopt (sfd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle)
      || setsockopt (sfd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof intvl)
      || setsockopt (sfd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof cnt))
    pr_err (1, "setsockopt");

  if ((tfd = net_tcp_connect (tblhost, TABLE_PORT)) < 0)
    pr_err (1, "net_tcp_connect: can't reach table (%s)", tblhost);

  if (table_protocol_enroll (tfd, ip, port, &key) < 0)
    pr_err (1, "table_protocol_enroll: enroll failed (%s:%s)", ip, port);

  pthread_mutex_lock (&slots[key].slock);

  /* take over the slot  */
  if (slots[key].sfd >= 0)
    {
      shutdown (slots[key].sfd, SHUT_RDWR); /* wake up old manager thread  */
      pr_debugx ("%s:%s (key %lu) takeover", ip, port, key);
    }

  slots[key].sfd = sfd;

  if (table_protocol_up (tfd, key) < 0)
    pr_err (1, "table_protocol_up: up failed (key %lu)", key);

  pthread_mutex_unlock (&slots[key].slock);

  pr_debugx ("%s:%s (key %lu) state up", ip, port, key);

  /* this only returns when the connection dies  */
  while ((n = recv (sfd, &c, 1, 0)) > 0 || (n < 0 && errno == EINTR))
    ;

  if (n == 0)
    pr_debugx ("%s:%s (key %lu) connection closed", ip, port, key);

  else
    pr_debug ("%s:%s (key %lu) connection lost", ip, port, key);

  pthread_mutex_lock (&slots[key].slock);

  /* check if another thread took over  */
  if (slots[key].sfd == sfd)
    {
      if (table_protocol_down (tfd, key) < 0)
        pr_err (1, "table_protocol_down: down failed (key %lu)", key);

      slots[key].sfd = -1;
      pr_debugx ("%s:%s (key %lu) state down", ip, port, key);
    }

  pthread_mutex_unlock (&slots[key].slock);

  close (sfd);
  close (tfd);

  return;

err_strlen:
err_strchr:
err_net_io_recvln:
  close (sfd);
}

int
main (int argc, char *argv[])
{
  int lfd;
  size_t i;

  if (argc != 3)
    usage ();

  tblhost = argv[2];

  for (i = 0; i < TABLE_LENGTH; ++i)
    {
      pthread_mutex_init (&slots[i].slock, NULL);
      slots[i].sfd = -1;
    }

  if ((lfd = net_tcp_listen (argv[1], MONITOR_PORT, INT_MAX)) < 0)
    pr_err (1, "net_tcp_listen: can't listen on %s:%s", argv[1], MONITOR_PORT);

  pr_debugx ("listening on %s:%s", argv[1], MONITOR_PORT);

  for (;;)
    if (net_tcp_serve (lfd, manage) < 0)
      pr_warn ("net_tcp_serve");
}
