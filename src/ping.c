#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <log/pr.h>
#include <net/io.h>
#include <net/tcp.h>
#include <ping/protocol.h>

[[noreturn]]
static void
usage (void)
{
  fprintf (stderr, "usage: ping <host> <port> <k>\n");
  exit (2);
}

int
main (int argc, char *argv[])
{
  char msg[PING_MSGBUFSZ], *end;
  const char *host, *port;
  unsigned long k, i;
  int servfd;

  if (argc != 4)
    usage ();

  host = argv[1];
  port = argv[2];

  errno = 0;
  k = strtoul (argv[3], &end, 10);

  if (end == argv[3] || *end != '\0' || errno == ERANGE)
    usage ();

  if ((servfd = net_tcp_connect (host, port)) < 0)
    pr_err (1, "net_tcp_connect: can't reach %s:%s", host, port);

  /* one ping per second  */
  for (i = 0; i < k; ++i)
    {
      if (net_io_sendln (servfd, PING, sizeof PING) < 0)
        pr_err (1, "net_io_sendln");

      if (net_io_recvln (servfd, msg, sizeof msg) < 0)
        pr_err (1, "net_io_recvln");

      puts (msg);
      sleep (1);
    }

  close (servfd);

  return 0;
}
