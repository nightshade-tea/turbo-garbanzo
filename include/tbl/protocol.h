#ifndef TBL_PROTOCOL_H
#define TBL_PROTOCOL_H

#include <netinet/in.h>

#define TBL_LENGTH 4096
#define TBL_PORT "3030"
#define TBL_MSGBUFSZ 128

#define TBL_POLICY_LC "lc"
#define TBL_POLICY_RR "rr"

#define TBL_ADDRSTRLEN INET_ADDRSTRLEN
#define TBL_PORTSTRLEN 6

struct server
{
  char ip[TBL_ADDRSTRLEN];
  char port[TBL_PORTSTRLEN];
  _Atomic unsigned char state; /* 0 down, 1 up  */
  _Atomic unsigned long conns;
};

/* get the key for ip:port, creating the entry if needed  */
int tbl_enroll (int fd, const char *ip, const char *port, unsigned long *key);

/* mark key as up  */
int tbl_up (int fd, unsigned long key);

/* mark key as down  */
int tbl_down (int fd, unsigned long key);

/* pick an up server by policy ("lc" or "rr") and add a conn to it.
   returns 1 if no server is up.  */
int tbl_grab (int fd, const char *policy, unsigned long *key,
              struct server *srv);

/* remove a conn from key  */
int tbl_release (int fd, unsigned long key);

/* get the entry for key  */
int tbl_status (int fd, unsigned long key, struct server *srv);

/* get the total # of conns  */
int tbl_total (int fd, unsigned long *total);

#endif /* TBL_PROTOCOL_H  */
