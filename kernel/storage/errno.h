#ifndef ZEROOS_STORAGE_ERRNO_H
#define ZEROOS_STORAGE_ERRNO_H

/* Kernel-internal storage error codes (negated in return values). Values
 * match the public ABI enum zeroos_error so syscalls pass them through. */
#define SE_PERM 1
#define SE_NOENT 2
#define SE_INTR 4
#define SE_IO 5
#define SE_NXIO 6
#define SE_BADF 9
#define SE_AGAIN 11
#define SE_NOMEM 12
#define SE_ACCES 13
#define SE_FAULT 14
#define SE_BUSY 16
#define SE_EXIST 17
#define SE_XDEV 18
#define SE_NODEV 19
#define SE_NOTDIR 20
#define SE_ISDIR 21
#define SE_INVAL 22
#define SE_NFILE 23
#define SE_MFILE 24
#define SE_FBIG 27
#define SE_NOSPC 28
#define SE_SPIPE 29
#define SE_ROFS 30
#define SE_MLINK 31
#define SE_NAMETOOLONG 36
#define SE_NOSYS 38
#define SE_NOTEMPTY 39
#define SE_OVERFLOW 75
#define SE_NOTSUP 95
#define SE_TIMEDOUT 110
#define SE_STALE 116
#define SE_UCLEAN 117
#define SE_CANCELED 125

#endif
