/**
 * src/monitor.c
 *
 * the monitor is responsible for enrolling and managing the state of the
 * servers in table, so whenever a server starts it has to set its state to
 * up, and whenever a server stops/crashes it has to set its state to down.
 *
 * to achieve this, the monitor listens on MON_PORT and the servers connect
 * to it on startup. each server connection (servfd) gets a manager thread that
 * enrolls the server on table and checks that the server is alive by setting
 * SO_KEEPALIVE on servfd. if a server dies, the keepalive will fail and the
 * manager thread wakes up and uses its connection to table (tblfd) to set that
 * server's state as down.
 *
 * server --- ip:port ---> monitor --- enroll ip:port, up/down key --> table
 *
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>

#include <log/pr.h>
#include <net/io.h>
#include <net/tcp.h>
#include <mon/protocol.h>
#include <tbl/protocol.h>

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
 * for that server in table and sets slot[key].servfd with the current
 * connection's socket file descriptor.
 *
 * to guarantee that, we make the manager threads grab slot[key].slock before
 * setting the server's state, and only do that when slot[key].servfd is the
 * same as its local servfd variable. that way, the new thread always sets the
 * state last and slot[key].servfd isn't clobbered.
 *
 * and having slot[key].servfd store the current manager thread's server
 * connection file descriptor allows the new thread to close the old thread's
 * connection, so it wakes up and dies.
 *
 */

static struct slot
{
  pthread_mutex_t slock; /* guards servfd and the key's up/down  */
  int servfd;            /* connection watching the key, or -1  */
} slots[TBL_LENGTH];     /* one slot for each key  */

static const char *tblhost;

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: monitor <ip> <tblhost>\n");
  exit (2);
}

static void
manage (int servfd)
{
  char msg[MON_MSGBUFSZ], *ip, *port;
  unsigned long key;
  int tblfd;

  /* server sends "<ip> <port>"  */
  if (net_io_recvln (servfd, msg, sizeof msg) < 0)
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

  if (strlen (ip) >= TBL_ADDRSTRLEN || strlen (port) >= TBL_PORTSTRLEN)
    {
      pr_warnx ("ip:port pair is too long (%s:%s)", ip, port);
      goto err_strlen;
    }

  /* 3s idle + 2 probes 1s apart  */
  if (net_tcp_keepalive (servfd, 3, 1, 2) < 0)
    pr_err (1, "net_tcp_keepalive");

  if ((tblfd = net_tcp_connect (tblhost, TBL_PORT)) < 0)
    pr_err (1, "net_tcp_connect: can't reach table (%s)", tblhost);

  if (tbl_enroll (tblfd, ip, port, &key) < 0)
    pr_err (1, "tbl_enroll: enroll failed (%s:%s)", ip, port);

  pthread_mutex_lock (&slots[key].slock);

  /* take over the slot  */
  if (slots[key].servfd >= 0)
    {
      /* wake up old manager thread  */
      shutdown (slots[key].servfd, SHUT_RDWR);
      pr_debugx ("%s:%s (key %lu) takeover", ip, port, key);
    }

  slots[key].servfd = servfd;

  if (tbl_up (tblfd, key) < 0)
    pr_err (1, "tbl_up: up failed (key %lu)", key);

  pthread_mutex_unlock (&slots[key].slock);

  pr_debugx ("%s:%s (key %lu) state up", ip, port, key);

  /* this only returns when the connection dies  */
  if (net_io_hold (servfd) < 0)
    pr_debug ("%s:%s (key %lu) connection lost", ip, port, key);

  else
    pr_debugx ("%s:%s (key %lu) connection closed", ip, port, key);

  pthread_mutex_lock (&slots[key].slock);

  /* check if another thread took over  */
  if (slots[key].servfd == servfd)
    {
      if (tbl_down (tblfd, key) < 0)
        pr_err (1, "tbl_down: down failed (key %lu)", key);

      slots[key].servfd = -1;
      pr_debugx ("%s:%s (key %lu) state down", ip, port, key);
    }

  pthread_mutex_unlock (&slots[key].slock);

  close (servfd);
  close (tblfd);

  return;

err_strlen:
err_strchr:
err_net_io_recvln:
  close (servfd);
}

int
main (int argc, char *argv[])
{
  const char *ip;
  int listenfd;
  size_t i;

  if (argc != 3)
    usage ();

  ip = argv[1];
  tblhost = argv[2];

  for (i = 0; i < TBL_LENGTH; ++i)
    {
      pthread_mutex_init (&slots[i].slock, NULL);
      slots[i].servfd = -1;
    }

  if ((listenfd = net_tcp_listen (ip, MON_PORT, INT_MAX)) < 0)
    pr_err (1, "net_tcp_listen: can't listen on %s:%s", ip, MON_PORT);

  pr_debugx ("listening on %s:%s", ip, MON_PORT);

  for (;;)
    if (net_tcp_serve (listenfd, manage) < 0)
      pr_warn ("net_tcp_serve");
}
