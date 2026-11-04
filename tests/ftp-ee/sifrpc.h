#ifndef FTP_EE_TEST_SIFRPC_H
#define FTP_EE_TEST_SIFRPC_H
/* Minimal EE-side SIF RPC client surface for the ftp_session host fixture. */
typedef struct SifRpcClientData {
	void *server;
	int unused;
} SifRpcClientData_t;
#define SIF_RPC_M_NOWAIT 1
int SifBindRpc(SifRpcClientData_t *client, unsigned int rpc_id, int mode);
int SifCheckStatRpc(SifRpcClientData_t *client);
int SifCallRpc(SifRpcClientData_t *client, unsigned int number, int mode,
			   void *send, int ssize, void *recv, int rsize, void *cb, void *ud);
#endif
