#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

#include <net/tcp.h>

static int
eai_errno (int rc)
{
  switch (rc)
    {
    case EAI_SYSTEM:
      return errno;
    case EAI_AGAIN:
      return EAGAIN;
    case EAI_MEMORY:
      return ENOMEM;
    case EAI_NONAME:
    case EAI_NODATA:
    case EAI_ADDRFAMILY:
    case EAI_FAIL:
      return EHOSTUNREACH;
    default: /* EAI_SERVICE, EAI_FAMILY, EAI_SOCKTYPE, EAI_BADFLAGS  */
      return EINVAL;
    }
}

int
net_tcp_listen (const char *host, const char *port, int backlog)
{
  struct addrinfo hints = { 0 }, *res;
  int rc, sfd, one = 1;

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((rc = getaddrinfo (host, port, &hints, &res)))
    {
      errno = eai_errno (rc);
      return -1;
    }

  if ((sfd = socket (res->ai_family, res->ai_socktype, res->ai_protocol)) < 0)
    goto err_socket;

  if (setsockopt (sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) < 0)
    goto err_setsockopt;

  if (bind (sfd, res->ai_addr, res->ai_addrlen) < 0)
    goto err_bind;

  if (listen (sfd, backlog) < 0)
    goto err_listen;

  freeaddrinfo (res);
  return sfd;

err_listen:
err_bind:
err_setsockopt:
  close (sfd);
err_socket:
  freeaddrinfo (res);
  return -1;
}

int
net_tcp_connect (const char *host, const char *port)
{
  struct addrinfo hints = { 0 }, *res;
  int rc, sfd;

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if ((rc = getaddrinfo (host, port, &hints, &res)))
    {
      errno = eai_errno (rc);
      return -1;
    }

  if ((sfd = socket (res->ai_family, res->ai_socktype, res->ai_protocol)) < 0)
    goto err_socket;

  if (connect (sfd, res->ai_addr, res->ai_addrlen) < 0)
    goto err_connect;

  freeaddrinfo (res);
  return sfd;

err_connect:
  close (sfd);
err_socket:
  freeaddrinfo (res);
  return -1;
}
