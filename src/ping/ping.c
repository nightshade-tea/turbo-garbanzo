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
  fprintf (stderr, "usage: ping <host> <port>\n");
  exit (2);
}

int
main (int argc, char *argv[])
{
  char line[64];
  int sock;

  if (argc != 3)
    usage ();

  if ((sock = net_tcp_connect (argv[1], argv[2])) < 0)
    return 1;

  if (net_io_sendln (sock, PING, sizeof PING) < 0)
    err (1, "send");

  if (net_io_recvln (sock, line, sizeof line) < 0)
    err (1, "recv");

  puts (line);
  close (sock);

  return 0;
}
