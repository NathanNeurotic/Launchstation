/* Host fixture for the production EE FTP session RPC client (src/ftp_session.c).
 * The SIF RPC surface is controllable so timeouts, in-flight reuse, version
 * mismatches, cooperative shutdown and IOP resets are all exercised against
 * the real state machine, not a reimplementation. This is a host contract
 * test; it does not emulate a PS2 or an IOP module.
 */
#include "ftp_session.h"
#include <sifrpc.h>
#include <delaythread.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned int iop_generation;
unsigned int getIopResetGeneration(void)
{
	return iop_generation;
}

static int bind_calls, bind_fail, bind_stalls;
static int call_calls, call_fail, call_stalls;
static int rpc_in_flight;
static SifRpcClientData_t *bind_client;
static void *rpc_recv;
static unsigned int last_command;
static long checks_until_complete = 1; /* polls before the in-flight RPC lands */
static int delay_calls;
static LsFtpStatus reply;
static int stop_after_queries; /* cooperative stop: status polls until STOPPED */

static void complete_rpc(void)
{
	assert(rpc_in_flight);
	if (last_command == LS_FTP_STOP)
		reply.state = LS_FTP_STOPPING;
	else if (last_command == LS_FTP_STATUS && stop_after_queries > 0 && --stop_after_queries == 0)
		reply.state = LS_FTP_STOPPED;
	if (rpc_recv)
		memcpy(rpc_recv, &reply, sizeof(reply));
	else if (bind_client)
		bind_client->server = (void *)1; /* a completed bind publishes the server */
	rpc_in_flight = 0;
}

static void land_in_flight(void)
{
	checks_until_complete = 1;
}

int SifBindRpc(SifRpcClientData_t *client, unsigned int rpc_id, int mode)
{
	bind_calls++;
	assert(rpc_id == LS_FTP_RPC_ID);
	assert(mode == SIF_RPC_M_NOWAIT);
	if (bind_fail)
		return -1;
	bind_client = client;
	rpc_recv = NULL;
	last_command = 0;
	rpc_in_flight = 1;
	checks_until_complete = bind_stalls ? 100000 : 1;
	return 0;
}

int SifCheckStatRpc(SifRpcClientData_t *client)
{
	(void)client;
	if (rpc_in_flight && --checks_until_complete <= 0)
		complete_rpc();
	return rpc_in_flight;
}

int SifCallRpc(SifRpcClientData_t *client, unsigned int number, int mode,
			   void *send, int ssize, void *recv, int rsize, void *cb, void *ud)
{
	call_calls++;
	assert(client->server != NULL);
	assert(mode == SIF_RPC_M_NOWAIT);
	assert(send == NULL && ssize == 0);
	assert(recv != NULL && rsize == (int)sizeof(LsFtpStatus));
	assert(cb == NULL && ud == NULL);
	if (call_fail)
		return -1;
	last_command = number;
	rpc_recv = recv;
	rpc_in_flight = 1;
	checks_until_complete = call_stalls ? 100000 : 1;
	return 0;
}

int DelayThread(unsigned int usec)
{
	(void)usec;
	delay_calls++;
	return 0;
}

/* The unit caches binding state in module-static storage keyed by the IOP
 * reset generation; bumping the generation makes its next call reinitialize
 * through the production checkGeneration() path, exactly like an IOP reset. */
static void reset_fixture(void)
{
	bind_calls = bind_fail = bind_stalls = 0;
	call_calls = call_fail = call_stalls = 0;
	rpc_in_flight = 0;
	bind_client = NULL;
	rpc_recv = NULL;
	last_command = 0;
	checks_until_complete = 1;
	delay_calls = 0;
	stop_after_queries = 0;
	memset(&reply, 0, sizeof(reply));
	reply.version = LS_FTP_PROTOCOL_VERSION;
	reply.state = LS_FTP_RUNNING;
	iop_generation++;
}

int main(void)
{
	LsFtpStatus status;

	/* Happy path: bind, one query, status returned. */
	reset_fixture();
	assert(ftpSessionQuery(&status) == 1);
	assert(status.state == LS_FTP_RUNNING && status.version == LS_FTP_PROTOCOL_VERSION);
	assert(bind_calls == 1 && call_calls == 1);
	assert(ftpSessionStopped() == 0);
	printf("PASS: query binds and returns status\n");

	/* Bind failure: no RPC issued, and the next attempt retries cleanly. */
	reset_fixture();
	bind_fail = 1;
	assert(ftpSessionQuery(&status) == 0);
	assert(bind_calls == 1 && call_calls == 0);
	bind_fail = 0;
	assert(ftpSessionQuery(&status) == 1);
	assert(bind_calls == 2);
	printf("PASS: bind failure is reported and recoverable\n");

	/* Bind timeout: exactly 1000 bounded waits, then failure; the stalled
	 * transaction is never reused, and recovery rebinds cleanly. */
	reset_fixture();
	bind_stalls = 1;
	assert(ftpSessionQuery(&status) == 0);
	assert(delay_calls == 1000);
	assert(ftpSessionQuery(&status) == 0); /* still in flight: no second bind */
	assert(bind_calls == 1 && call_calls == 0);
	bind_stalls = 0;
	land_in_flight(); /* the stalled bind finally lands */
	assert(ftpSessionQuery(&status) == 1);
	assert(bind_calls == 2 && call_calls == 1);
	printf("PASS: bind timeout is bounded and never reuses DMA state\n");

	/* Call timeout: a query while one is in flight must not issue another
	 * RPC into the same static packet. */
	reset_fixture();
	assert(ftpSessionQuery(&status) == 1); /* bound, one clean call */
	call_stalls = 1;
	assert(ftpSessionQuery(&status) == 0); /* times out: exactly 1000 waits */
	assert(call_calls == 2 && delay_calls == 1000);
	assert(ftpSessionQuery(&status) == 0); /* still in flight: no new RPC */
	assert(call_calls == 2 && delay_calls == 1000);
	call_stalls = 0;
	land_in_flight();                      /* old call finally lands */
	assert(ftpSessionQuery(&status) == 1); /* pending cleared, new RPC issued */
	assert(call_calls == 3);
	printf("PASS: in-flight call is never reissued into the static packet\n");

	/* Version mismatch is rejected without latching stop state. */
	reset_fixture();
	reply.version = 99;
	assert(ftpSessionQuery(&status) == 0);
	assert(ftpSessionStopped() == 0);
	printf("PASS: protocol version mismatch rejected\n");

	/* STOPPED latches; shutdown then short-circuits without any RPC. */
	reset_fixture();
	reply.state = LS_FTP_STOPPED;
	assert(ftpSessionQuery(&status) == 1);
	assert(ftpSessionStopped() == 1);
	{
		int calls_before = call_calls;
		assert(ftpSessionShutdown() == 1);
		assert(call_calls == calls_before);
	}
	printf("PASS: STOPPED latches and shutdown short-circuits\n");

	/* Cooperative shutdown: STOP, then bounded polling until STOPPED. */
	reset_fixture();
	stop_after_queries = 3;
	assert(ftpSessionShutdown() == 1);
	assert(call_calls == 1 /* STOP */ + 3 /* status polls */);
	assert(last_command == LS_FTP_STATUS);
	printf("PASS: cooperative stop reaches STOPPED\n");

	/* A server that never stops keeps shutdown bounded: 100 polls, then 0. */
	reset_fixture();
	stop_after_queries = -1; /* never reaches STOPPED */
	assert(ftpSessionShutdown() == 0);
	assert(call_calls == 1 + 100);
	printf("PASS: shutdown polling is bounded at 100\n");

	/* An IOP reset rebinds and clears latched state. */
	reset_fixture();
	reply.state = LS_FTP_STOPPED;
	assert(ftpSessionQuery(&status) == 1);
	assert(ftpSessionStopped() == 1);
	iop_generation++;
	reply.state = LS_FTP_RUNNING;
	assert(ftpSessionStopped() == 0); /* generation check cleared the latch */
	assert(ftpSessionQuery(&status) == 1);
	assert(bind_calls == 2); /* rebound after the reset */
	assert(ftpSessionStopped() == 0);
	printf("PASS: IOP reset generation rebinds and clears state\n");

	printf("ftp-session tests passed\n");
	return 0;
}
