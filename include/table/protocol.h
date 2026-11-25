#ifndef TABLE_PROTOCOL_H
#define TABLE_PROTOCOL_H

#include <netinet/in.h>

#define TABLE_PORT "3030"

#define TABLE_ADDRSTRLEN INET_ADDRSTRLEN
#define TABLE_PORTSTRLEN 6

struct server
{
  char ip[TABLE_ADDRSTRLEN];
  char port[TABLE_PORTSTRLEN];
  _Atomic unsigned char state; /* 0 down, 1 up  */
  _Atomic unsigned long conns;
};

/* get the key for ip:port, creating the entry if needed.
   returns 0 or -1 on error.  */
int table_protocol_enroll (int sock, const char *ip, const char *port,
                           unsigned long *key);

/* mark key as up. returns 0 or -1 on error.  */
int table_protocol_up (int sock, unsigned long key);

/* mark key as down. returns 0 or -1 on error.  */
int table_protocol_down (int sock, unsigned long key);

/* pick an up server by policy ("lc" or "rr") and add a conn to it.
   returns 0, 1 if no server is up or -1 on error.  */
int table_protocol_grab (int sock, const char *policy, unsigned long *key,
                         struct server *srv);

/* remove a conn from key. returns 0 or -1 on error.  */
int table_protocol_release (int sock, unsigned long key);

/* get the entry for key. returns 0 or -1 on error.  */
int table_protocol_status (int sock, unsigned long key, struct server *srv);

/* get the total # of conns. returns 0 or -1 on error.  */
int table_protocol_total (int sock, unsigned long *total);

#endif /* TABLE_PROTOCOL_H  */
