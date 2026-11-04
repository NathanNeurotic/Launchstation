#ifndef FTP_TEST_SIFRPC_H
#define FTP_TEST_SIFRPC_H
#include <stddef.h>
typedef struct {
	int value;
} SifRpcDataQueue_t;
typedef struct {
	int value;
} SifRpcServerData_t;
void sceSifSetRpcQueue(SifRpcDataQueue_t *, int);
void sceSifRegisterRpc(SifRpcServerData_t *, int, void *(*)(int, void *, int), void *, void *, void *,
					   SifRpcDataQueue_t *);
void sceSifRpcLoop(SifRpcDataQueue_t *);
#endif
