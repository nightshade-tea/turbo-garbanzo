#ifndef MON_PROTOCOL_H
#define MON_PROTOCOL_H

#define MON_PORT "3031"

/* enroll ip:port on monhost  */
int mon_enroll (const char *monhost, const char *ip, const char *port,
                void (*lostfn) (void));

#endif /* MON_PROTOCOL_H  */
