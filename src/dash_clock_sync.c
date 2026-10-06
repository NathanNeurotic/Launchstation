//--------------------------------------------------------------
// PS2 RTC synchronization over SNTP.
// Demand-driven: no network modules are started until the user asks.
//--------------------------------------------------------------
#include "launchelf.h"
#include "dash_internal.h"
#include "init.h"
#include "main_title.h"

#ifdef ETH

#include <ps2ips.h>
#include <string.h>
#include <stdlib.h>

#define SNTP_PACKET_SIZE 48
#define SNTP_PORT 123
#define SNTP_TIMEOUT_MS 1800
#define SNTP_EPOCH_DELTA 2208988800ULL
#define UNIX_2020 1577836800ULL
#define UNIX_2100 4102444800ULL

struct hostent;
extern int ps2ipc_disconnect(int s);
extern int ps2ipc_recvfrom(int s, void *mem, int len, unsigned int flags, struct sockaddr *from, int *fromlen);
extern int ps2ipc_sendto(int s, const void *dataptr, int size, unsigned int flags, const struct sockaddr *to, int tolen);
extern int ps2ipc_socket(int domain, int type, int protocol);
extern int ps2ipc_select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout);
extern struct hostent *ps2ipc_gethostbyname(const char *name);

typedef struct {
	char *h_name;
	char **h_aliases;
	int h_addrtype;
	int h_length;
	char **h_addr_list;
} SntpHostEnt;

typedef struct {
	u8 sin_len;
	u8 sin_family;
	u16 sin_port;
	u32 sin_addr;
	char sin_zero[8];
} SntpSockaddrIn;

static u16 sntpHtons(u16 v)
{
	return (u16)((v << 8) | (v >> 8));
}

static u32 getBe32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static void putBe32(u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

static int sntpWaitReadable(int sock)
{
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	tv.tv_sec = SNTP_TIMEOUT_MS / 1000;
	tv.tv_usec = (SNTP_TIMEOUT_MS % 1000) * 1000;
	return ps2ipc_select(sock + 1, &fds, NULL, NULL, &tv) > 0 && FD_ISSET(sock, &fds);
}

static int sntpQuery(const char *host, u64 *unix_time)
{
	SntpHostEnt *he;
	SntpSockaddrIn addr, from;
	u8 req[SNTP_PACKET_SIZE], reply[SNTP_PACKET_SIZE];
	u32 nonce_hi, nonce_lo, ntp_sec, ntp_frac;
	u64 full_ntp, unix_sec;
	int sock = -1, fromlen = sizeof(from), got;

	if (host == NULL || unix_time == NULL)
		return 0;
	he = (SntpHostEnt *)(void *)ps2ipc_gethostbyname(host);
	if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL)
		return 0;

	memset(req, 0, sizeof(req));
	req[0] = 0x23; /* LI=0, VN=4, mode=client */
	nonce_hi = (u32)Timer() ^ 0x574C4554U;
	nonce_lo = nonce_hi * 1664525U + 1013904223U;
	putBe32(req + 40, nonce_hi);
	putBe32(req + 44, nonce_lo);

	memset(&addr, 0, sizeof(addr));
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_port = sntpHtons(SNTP_PORT);
	memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));

	sock = ps2ipc_socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return 0;
	if (ps2ipc_sendto(sock, req, sizeof(req), 0, (const struct sockaddr *)&addr, sizeof(addr)) != sizeof(req) ||
	    !sntpWaitReadable(sock)) {
		ps2ipc_disconnect(sock);
		return 0;
	}

	memset(&from, 0, sizeof(from));
	got = ps2ipc_recvfrom(sock, reply, sizeof(reply), 0, (struct sockaddr *)&from, &fromlen);
	ps2ipc_disconnect(sock);
	if (got < SNTP_PACKET_SIZE)
		return 0;
	if ((reply[0] >> 6) == 3 || ((reply[0] & 7) != 4 && (reply[0] & 7) != 5))
		return 0;
	if (reply[1] == 0 || reply[1] > 15)
		return 0;
	/* A valid server echoes our transmit timestamp as its origin timestamp. */
	if (memcmp(reply + 24, req + 40, 8) != 0)
		return 0;

	ntp_sec = getBe32(reply + 40);
	ntp_frac = getBe32(reply + 44);
	full_ntp = ntp_sec;
	/* NTP era 1 begins in 2036. Values below the halfway mark are interpreted
	 * as post-2036 once era 0 would predate our accepted range. */
	if (ntp_sec < 0x80000000U)
		full_ntp += 0x100000000ULL;
	if (full_ntp < SNTP_EPOCH_DELTA)
		return 0;
	unix_sec = full_ntp - SNTP_EPOCH_DELTA;
	if (ntp_frac >= 0x80000000U)
		unix_sec++;
	if (unix_sec < UNIX_2020 || unix_sec >= UNIX_2100)
		return 0;
	*unix_time = unix_sec;
	return 1;
}

static s64 civilDays(int year, unsigned month, unsigned day)
{
	int era, yoe;
	unsigned doy, doe;

	year -= month <= 2;
	era = (year >= 0 ? year : year - 399) / 400;
	yoe = year - era * 400;
	doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	doe = (unsigned)yoe * 365 + (unsigned)yoe / 4 - (unsigned)yoe / 100 + doy;
	return (s64)era * 146097 + doe - 719468;
}

static void civilFromDays(s64 z, int *year, int *month, int *day)
{
	s64 era;
	unsigned doe, yoe, doy, mp;
	int y, m;

	z += 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = (unsigned)(z - era * 146097);
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = (int)yoe + (int)era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*day = (int)(doy - (153 * mp + 2) / 5 + 1);
	m = (int)mp + (mp < 10 ? 3 : -9);
	*month = m;
	*year = y + (m <= 2);
}

static u8 toBcd(int v)
{
	return (u8)(((v / 10) << 4) | (v % 10));
}

static int fromBcd(u8 v)
{
	return ((v >> 4) & 15) * 10 + (v & 15);
}

static int clockLocalSeconds(const sceCdCLOCK *c, s64 *out)
{
	int year, month, day, hour, minute, second;
	s64 days;

	if (c == NULL || out == NULL || c->stat)
		return 0;
	year = 2000 + fromBcd(c->year);
	month = fromBcd(c->month & 0x7f);
	day = fromBcd(c->day);
	hour = fromBcd(c->hour);
	minute = fromBcd(c->minute);
	second = fromBcd(c->second);
	if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
	    hour > 23 || minute > 59 || second > 59)
		return 0;
	days = civilDays(year, (unsigned)month, (unsigned)day);
	*out = days * 86400 + hour * 3600 + minute * 60 + second;
	return 1;
}

static int writePs2Clock(u64 unix_utc)
{
	const u64 jst = unix_utc + 9ULL * 3600ULL;
	s64 days = (s64)(jst / 86400ULL);
	int sod = (int)(jst % 86400ULL);
	int year, month, day;
	sceCdCLOCK set, check;
	s64 target_local, check_local, diff;

	civilFromDays(days, &year, &month, &day);
	if (year < 2000 || year > 2099)
		return 0;
	memset(&set, 0, sizeof(set));
	set.second = toBcd(sod % 60);
	set.minute = toBcd((sod / 60) % 60);
	set.hour = toBcd(sod / 3600);
	set.day = toBcd(day);
	set.month = toBcd(month);
	set.year = toBcd(year % 100);
	if (!sceCdWriteClock(&set))
		return 0;
	memset(&check, 0, sizeof(check));
	if (!sceCdReadClock(&check))
		return 0;
	target_local = (s64)jst;
	if (!clockLocalSeconds(&check, &check_local))
		return 0;
	diff = check_local - target_local;
	if (diff < 0)
		diff = -diff;
	return diff <= 2;
}

int dashClockSyncInternet(char *result, int result_size)
{
	static const char *const servers[] = {
	    "time.google.com",
	    "time.cloudflare.com",
	    "pool.ntp.org",
	};
	u64 samples[3];
	int ok[3] = {0, 0, 0};
	int i, j, best_i = -1, best_j = -1;
	u64 best_diff = ~0ULL, chosen;

	if (result != NULL && result_size > 0)
		result[0] = '\0';
	if (!ensureDashboardIpNetworkReady()) {
		if (result) snprintf(result, result_size, "Network stack could not be started.");
		return 0;
	}
	for (i = 0; i < 3; i++) {
		char status[96];
		snprintf(status, sizeof(status), "Checking time source %d of 3...", i + 1);
		dashLoadingFrame(status, i, 3);
		ok[i] = sntpQuery(servers[i], &samples[i]);
	}
	for (i = 0; i < 3; i++) {
		if (!ok[i])
			continue;
		for (j = i + 1; j < 3; j++) {
			u64 diff;
			if (!ok[j])
				continue;
			diff = samples[i] > samples[j] ? samples[i] - samples[j] : samples[j] - samples[i];
			if (diff <= 10 && diff < best_diff) {
				best_diff = diff;
				best_i = i;
				best_j = j;
			}
		}
	}
	if (best_i < 0) {
		if (result) snprintf(result, result_size, "Could not get two Internet time sources to agree.");
		return 0;
	}
	chosen = (samples[best_i] + samples[best_j]) / 2ULL;
	if (!writePs2Clock(chosen)) {
		if (result) snprintf(result, result_size, "Internet time was received, but the PS2 RTC write could not be verified.");
		return 0;
	}
	menuTitleUpdateAsync(1);
	if (result) snprintf(result, result_size, "PS2 system clock synchronized and verified.");
	return 1;
}

#else

int dashClockSyncInternet(char *result, int result_size)
{
	if (result != NULL && result_size > 0)
		snprintf(result, result_size, "This build does not include Ethernet support.");
	return 0;
}

#endif
//--------------------------------------------------------------
//End of file: dash_clock_sync.c
//--------------------------------------------------------------
