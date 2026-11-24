#ifndef TABLE_PROTOCOL_H
#define TABLE_PROTOCOL_H

#define TABLE_PORT 3030

int get (int sock, unsigned int key, unsigned long *val);
int set (int sock, unsigned int key, unsigned long val);
int inc (int sock, unsigned int key);
int dec (int sock, unsigned int key);

#endif
