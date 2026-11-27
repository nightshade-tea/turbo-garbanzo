#include <errno.h>
#include <string.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <net/io.h>

ssize_t
net_io_send (int sock, const void *buf, size_t len)
{
  const char *p = buf;
  size_t off = 0;
  ssize_t n;

  while (off < len)
    {
      n = send (sock, p + off, len - off, MSG_NOSIGNAL);

      if (n < 0)
        {
          if (errno == EINTR)
            continue;

          return -1;
        }

      off += (size_t)n;
    }

  return (ssize_t)off;
}

ssize_t
net_io_sendln (int sock, const char *str, size_t len)
{
  size_t sent, slen = 0;
  ssize_t n;

  if (len == 0)
    return 0;

  while (slen < len && str[slen] != '\0' && str[slen] != '\n')
    ++slen;

  /* str and '\n' in one call, so they go out as one packet  */
  struct iovec iov[2] = {
    { .iov_base = (void *)str, .iov_len = slen },
    { .iov_base = (void *)"\n", .iov_len = 1 },
  };

  struct msghdr msg = { .msg_iov = iov, .msg_iovlen = 2 };

  while (msg.msg_iovlen > 0)
    {
      n = sendmsg (sock, &msg, MSG_NOSIGNAL);

      if (n < 0)
        {
          if (errno == EINTR)
            continue;

          return -1;
        }

      /* drop finished iovecs, advance the partial one  */
      sent = (size_t)n;

      while (msg.msg_iovlen > 0 && sent >= msg.msg_iov->iov_len)
        {
          sent -= msg.msg_iov->iov_len;
          ++msg.msg_iov;
          --msg.msg_iovlen;
        }

      if (msg.msg_iovlen > 0)
        {
          msg.msg_iov->iov_base = (char *)msg.msg_iov->iov_base + sent;
          msg.msg_iov->iov_len -= sent;
        }
    }

  return (ssize_t)slen + 1;
}

ssize_t
net_io_recv (int sock, void *buf, size_t len)
{
  char *p = buf;
  size_t off = 0;
  ssize_t n;

  while (off < len)
    {
      n = recv (sock, p + off, len - off, 0);

      if (n < 0)
        {
          if (errno == EINTR)
            continue;

          return -1;
        }

      if (n == 0)
        {
          if (off == 0)
            {
              errno = ENOTCONN;
              return -1;
            }

          break;
        }

      off += (size_t)n;
    }

  return (ssize_t)off;
}

/* consume up to and including the next '\n' or until eof.  */
static int
chomp (int sock)
{
  char tmp[512], *end;
  size_t drop;
  ssize_t n;

  for (;;)
    {
      if ((n = recv (sock, tmp, sizeof tmp, MSG_PEEK)) < 0)
        {
          if (errno == EINTR)
            continue;

          return -1;
        }

      if (n == 0)
        return 0;

      end = memchr (tmp, '\n', (size_t)n);
      drop = end != NULL ? (size_t)(end - tmp) + 1 : (size_t)n;

      if (net_io_recv (sock, tmp, drop) < 0)
        return -1;

      if (end != NULL)
        return 0;
    }
}

ssize_t
net_io_recvln (int sock, char *str, size_t len)
{
  char *nl;
  size_t i = 0, k;
  ssize_t ret, n;

  if (len == 0)
    return 0;

  /* peek, then consume only up to the '\n'  */
  for (;;)
    {
      n = recv (sock, str + i, len - i, MSG_PEEK);

      if (n < 0 && errno == EINTR)
        continue;

      if (n <= 0)
        {
          if (n == 0)
            {
              if (i == 0)
                errno = ENOTCONN;

              else
                errno = EBADMSG;
            }

          ret = -1;
          break;
        }

      nl = memchr (str + i, '\n', (size_t)n);

      if (nl != NULL)
        {
          k = (size_t)(nl - str) - i;

          if (net_io_recv (sock, str + i, k + 1) < 0)
            {
              ret = -1;
              break;
            }

          i += k;
          ret = (ssize_t)i + 1;
          break;
        }

      /* doesnt fit, keep what does and drop the rest of the line  */
      if (i + (size_t)n == len)
        {
          ret = -1;

          if (net_io_recv (sock, str + i, len - 1 - i) < 0)
            break;

          i = len - 1;

          if (chomp (sock) == 0)
            errno = EMSGSIZE;

          break;
        }

      if (net_io_recv (sock, str + i, (size_t)n) < 0)
        {
          ret = -1;
          break;
        }

      i += (size_t)n;
    }

  str[i] = '\0';
  return ret;
}

int
net_io_hold (int sock)
{
  ssize_t n;
  char c;

  while ((n = recv (sock, &c, 1, 0)) > 0 || (n < 0 && errno == EINTR))
    ;

  if (n < 0)
    return -1;

  return 0;
}

/* splice would be better if we needed more throughput, since it doesn't copy
   buffers to and from userspace, but recv/send keeps this simple.  */
int
net_io_relay (int afd, int bfd)
{
  struct pollfd pfd[2]
      = { { .fd = afd, .events = POLLIN }, { .fd = bfd, .events = POLLIN } };
  int fd[2] = { afd, bfd }, open = 2, i;
  char buf[4096];
  ssize_t n;

  while (open > 0)
    {
      if (poll (pfd, 2, -1) < 0)
        {
          if (errno == EINTR)
            continue;

          return -1;
        }

      for (i = 0; i < 2; ++i)
        {
          if (pfd[i].revents == 0)
            continue;

          if ((n = recv (fd[i], buf, sizeof buf, 0)) < 0 && errno == EINTR)
            continue;

          /* eof or error, pass it on and stop watching this side  */
          if (n <= 0)
            {
              shutdown (fd[!i], SHUT_WR);
              pfd[i].fd = -1;
              --open;
              continue;
            }

          if (net_io_send (fd[!i], buf, (size_t)n) < 0)
            return -1;
        }
    }

  return 0;
}
