#ifndef __OBJECT_HASH_H_
#define __OBJECT_HASH_H_

#define HEX_DIGEST_LENGTH	257
#define Z_CHUNK			16384

/* Commands */
#define CMD_HASH_OBJECT_WRITE	0x1

int hash_object_main(int argc, char *argv[]);

#endif