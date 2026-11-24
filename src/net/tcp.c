#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <err.h>

#include <net/tcp.h>

int
net_tcp_listen (const char *host, const char *port, int backlog)
{
  struct addrinfo hints = {0}, *res;
  int rc, sfd, one = 1;

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((rc = getaddrinfo (host, port, &hints, &res)))
    {
      if (rc == EAI_SYSTEM)
        warn ("getaddrinfo");

      else
        warnx ("getaddrinfo: %s", gai_strerror (rc));

      goto err_getaddrinfo;
    }

  if ((sfd = socket (res->ai_family, res->ai_socktype, res->ai_protocol)) < 0)
    {
      warn ("socket");
      goto err_socket;
    }

  if (setsockopt (sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one))
    {
      warn ("setsockopt");
      goto err_setsockopt;
    }

  if (bind (sfd, res->ai_addr, res->ai_addrlen) < 0)
    {
      warn ("bind");
      goto err_bind;
    }

  if (listen (sfd, backlog) < 0)
    {
      warn ("listen");
      goto err_listen;
    }

  freeaddrinfo (res);
  return sfd;

err_listen:
err_bind:
err_setsockopt:
  close (sfd);
err_socket:
  freeaddrinfo (res);
err_getaddrinfo:
  return -1;
}

int
net_tcp_connect (const char *host, const char *port)
{
  struct addrinfo hints = {0}, *res;
  int rc, sfd;

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if ((rc = getaddrinfo (host, port, &hints, &res)))
    {
      warnx ("getaddrinfo: %s", gai_strerror (rc));
      goto err_getaddrinfo;
    }

  if ((sfd = socket (res->ai_family, res->ai_socktype, res->ai_protocol)) < 0)
    {
      warn ("socket");
      goto err_socket;
    }

  if (connect (sfd, res->ai_addr, res->ai_addrlen))
    {
      warn ("connect");
      goto err_connect;
    }

  freeaddrinfo (res);
  return sfd;

err_connect:
  close (sfd);
err_socket:
  freeaddrinfo (res);
err_getaddrinfo:
  return -1;
}
