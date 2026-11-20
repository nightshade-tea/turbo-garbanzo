#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <err.h>

[[noreturn]]
static void
usage (void)
{
  errx (2, "print usage...");
}

int
main (int argc, [[maybe_unused]] char *argv[])
{
  if (argc != 2)
    usage ();

  return 0;
}
