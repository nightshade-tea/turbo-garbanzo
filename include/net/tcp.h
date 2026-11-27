#ifndef NET_TCP_H
#define NET_TCP_H

/* listen for connections on host:port  */
int net_tcp_listen (const char *host, const char *port, int backlog);

/* connect to host:port  */
int net_tcp_connect (const char *host, const char *port);

/* accept a connection on listenfd and spawn a connfn thread to handle it  */
int net_tcp_serve (int listenfd, void (*connfn) (int connfd));

/* enable keepalive on sock, probing cnt times intvl seconds apart after
   idle seconds  */
int net_tcp_keepalive (int sock, int idle, int intvl, int cnt);

#endif /* NET_TCP_H  */
