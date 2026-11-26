#ifndef NET_TCP_H
#define NET_TCP_H

/* listen for connections on host:port  */
int net_tcp_listen (const char *host, const char *port, int backlog);

/* connect to host:port  */
int net_tcp_connect (const char *host, const char *port);

#endif /* NET_TCP_H  */
