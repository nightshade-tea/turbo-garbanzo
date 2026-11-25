#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>

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
  unsigned long k, i;
  char line[64], *end;
  int sock;

  if (argc != 4)
    usage ();

  errno = 0;
  k = strtoul (argv[3], &end, 10);

  if (end == argv[3] || *end != '\0' || errno == ERANGE)
    usage ();

  if ((sock = net_tcp_connect (argv[1], argv[2])) < 0)
    return 1;

  /* one ping per second  */
  for (i = 0; i < k; ++i)
    {
      if (net_io_sendln (sock, PING, sizeof PING) < 0)
        err (1, "send");

      if (net_io_recvln (sock, line, sizeof line) < 0)
        err (1, "recv");

      puts (line);
      sleep (1);
    }

  close (sock);

  return 0;
}
