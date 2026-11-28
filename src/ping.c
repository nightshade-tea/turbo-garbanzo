/**
 * src/ping.c
 *
 * ping is the client used to test the load balancer. it connects to host:port
 * and sends count pings, one per second, printing which server replied and
 * the round trip time, then a summary.
 *
 * loadb relays the whole connection to a single server, so all replies
 * come from the same pong, unless it dies and loadb moves ping to another one.
 *
 * ping --- ping ---> loadb --- ping ---> pong
 *      <-- pong ip:port ---  <-- pong ip:port ---
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <log/pr.h>
#include <net/io.h>
#include <net/tcp.h>
#include <ping/protocol.h>

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: ping <host> <port> <count>\n");
  exit (2);
}

static double
now_ms (void)
{
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);

  return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

int
main (int argc, char *argv[])
{
  double start, rtt, min = 0, max = 0, sum = 0;
  char msg[PING_MSGBUFSZ], *end;
  const char *host, *port;
  unsigned long count, seq;
  int servfd;

  if (argc != 4)
    usage ();

  host = argv[1];
  port = argv[2];

  errno = 0;
  count = strtoul (argv[3], &end, 10);

  if (argv[3][0] == '-' || end == argv[3] || *end != '\0' || errno == ERANGE)
    usage ();

  if ((servfd = net_tcp_connect (host, port)) < 0)
    pr_err (1, "net_tcp_connect: can't reach %s:%s", host, port);

  for (seq = 0; seq < count; ++seq)
    {
      start = now_ms ();

      if (net_io_sendln (servfd, PING, sizeof PING) < 0)
        pr_err (1, "net_io_sendln");

      if (net_io_recvln (servfd, msg, sizeof msg) < 0)
        pr_err (1, "net_io_recvln");

      rtt = now_ms () - start;

      if (strncmp (msg, PONG " ", sizeof PONG) != 0)
        pr_errx (1, "bad reply (%s)", msg);

      printf ("pong from %s: seq=%lu time=%.3f ms\n", msg + sizeof PONG, seq,
              rtt);

      if (seq == 0 || rtt < min)
        min = rtt;

      if (rtt > max)
        max = rtt;

      sum += rtt;

      /* one ping per second  */
      if (seq + 1 < count)
        sleep (1);
    }

  close (servfd);

  if (count > 0)
    printf ("%lu pings, min/avg/max = %.3f/%.3f/%.3f ms\n", count, min,
            sum / (double)count, max);

  return 0;
}
