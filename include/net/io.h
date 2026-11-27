#ifndef NET_IO_H
#define NET_IO_H

#include <stddef.h>
#include <sys/types.h>

/* send len bytes from buf. returns # of bytes sent or -1 on error.  */
ssize_t net_io_send (int fd, const void *buf, size_t len);

/* recv len bytes to buf. returns # of bytes received or -1 on error.  */
ssize_t net_io_recv (int fd, void *buf, size_t len);

/* send str until '\n' or NUL, up to len characters, then '\n'.
   returns # of bytes sent or -1 on error.  */
ssize_t net_io_sendln (int fd, const char *str, size_t len);

/* recv until '\n', up to len - 1 characters (NUL terminated).
   returns # of bytes received or -1 on error.  */
ssize_t net_io_recvln (int fd, char *str, size_t len);

/* block until fd's connection drops  */
int net_io_hold (int fd);

/* relay data between afd and bfd  */
int net_io_relay (int afd, int bfd);

#endif /* NET_IO_H  */
