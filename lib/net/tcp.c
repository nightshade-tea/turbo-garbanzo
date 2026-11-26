#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

#include <net/tcp.h>

struct conn
{
  void (*connfn) (int connfd);
  int connfd;
};

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

static void *
conn_start (void *arg)
{
  struct conn c = *(struct conn *)arg;

  free (arg);
  c.connfn (c.connfd);

  return NULL;
}

int
net_tcp_serve (int listenfd, void (*connfn) (int connfd))
{
  struct conn *c;
  pthread_t t;
  int connfd, rc;

  if ((connfd = accept (listenfd, NULL, NULL)) < 0)
    return -1;

  if ((c = malloc (sizeof *c)) == NULL)
    goto err_malloc;

  c->connfn = connfn;
  c->connfd = connfd;

  if ((rc = pthread_create (&t, NULL, conn_start, c)))
    {
      errno = rc;
      goto err_pthread_create;
    }

  pthread_detach (t);
  return 0;

err_pthread_create:
  free (c);
err_malloc:
  close (connfd);
  return -1;
}
