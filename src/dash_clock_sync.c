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
#define SNTP_TIMEOUT_MS 5000
#define SNTP_ATTEMPTS 2
#define SNTP_EPOCH_DELTA 2208988800ULL
#define UNIX_2020 1577836800ULL
#define UNIX_2100 4102444800ULL

struct hostent;
extern int ps2ipc_disconnect(int s);
extern int ps2ipc_connect(int s, const struct sockaddr *name, int namelen);
extern int ps2ipc_recv(int s, void *mem, int len, unsigned int flags);
extern int ps2ipc_send(int s, const void *dataptr, int size, unsigned int flags);
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

/* Pair the received server time with the local elapsed timer, as in NTPS2. */
typedef struct {
	u64 unix_ms;
	u64 received_ms;
} SntpSample;

enum {
	SNTP_OK = 0,
	SNTP_DNS,
	SNTP_SOCKET,
	SNTP_CONNECT,
	SNTP_SEND,
	SNTP_WAIT,
	SNTP_TIMEOUT,
	SNTP_RECEIVE,
	SNTP_SHORT,
	SNTP_INVALID,
	SNTP_UNSYNCED,
	SNTP_REFUSED,
	SNTP_ORIGIN,
	SNTP_DATE,
	SNTP_TIMER
};

static const char *sntpStatusText(int status)
{
	switch (status) {
		case SNTP_OK: return "OK";
		case SNTP_DNS: return "DNS lookup failed";
		case SNTP_SOCKET: return "socket creation failed";
		case SNTP_CONNECT: return "server connection failed";
		case SNTP_SEND: return "request send failed";
		case SNTP_WAIT: return "socket wait failed";
		case SNTP_TIMEOUT: return "reply timed out";
		case SNTP_RECEIVE: return "reply receive failed";
		case SNTP_SHORT: return "short reply";
		case SNTP_INVALID: return "invalid reply";
		case SNTP_UNSYNCED: return "server unsynchronized";
		case SNTP_REFUSED: return "server refused request";
		case SNTP_ORIGIN: return "request/reply mismatch";
		case SNTP_DATE: return "server date out of range";
		case SNTP_TIMER: return "local timer restarted";
		default: return "not checked";
	}
}

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
	int ready;

	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	tv.tv_sec = SNTP_TIMEOUT_MS / 1000;
	tv.tv_usec = (SNTP_TIMEOUT_MS % 1000) * 1000;
	ready = ps2ipc_select(sock + 1, &fds, NULL, NULL, &tv);
	if (ready < 0)
		return SNTP_WAIT;
	if (ready == 0)
		return SNTP_TIMEOUT;
	return FD_ISSET(sock, &fds) ? SNTP_OK : SNTP_WAIT;
}

static int sntpQuery(const char *host, SntpSample *sample)
{
	SntpHostEnt *he;
	SntpSockaddrIn addr;
	u8 req[SNTP_PACKET_SIZE], reply[SNTP_PACKET_SIZE];
	u32 nonce_hi, nonce_lo, ntp_sec, ntp_frac;
	u64 full_ntp, unix_sec, sent_ms, received_ms;
	int sock, got, status;

	if (host == NULL || sample == NULL)
		return SNTP_INVALID;
	he = (SntpHostEnt *)(void *)ps2ipc_gethostbyname(host);
	if (he == NULL || he->h_addrtype != AF_INET || he->h_length != 4 ||
	    he->h_addr_list == NULL || he->h_addr_list[0] == NULL)
		return SNTP_DNS;

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
		return SNTP_SOCKET;
	/* Connected UDP limits replies to this server and uses the RPC receive
	 * path without sockaddr/request overlap, including SDK-provided servers. */
	if (ps2ipc_connect(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
		ps2ipc_disconnect(sock);
		return SNTP_CONNECT;
	}
	sent_ms = Timer();
	if (ps2ipc_send(sock, req, sizeof(req), 0) != sizeof(req)) {
		ps2ipc_disconnect(sock);
		return SNTP_SEND;
	}
	status = sntpWaitReadable(sock);
	if (status != SNTP_OK) {
		ps2ipc_disconnect(sock);
		return status;
	}

	got = ps2ipc_recv(sock, reply, sizeof(reply), 0);
	received_ms = Timer();
	ps2ipc_disconnect(sock);
	if (got < 0)
		return SNTP_RECEIVE;
	if (got < SNTP_PACKET_SIZE)
		return SNTP_SHORT;
	if (((reply[0] & 7) != 4 && (reply[0] & 7) != 5) || reply[1] > 15)
		return SNTP_INVALID;
	/* A valid server echoes our transmit timestamp as its origin timestamp. */
	if (memcmp(reply + 24, req + 40, 8) != 0)
		return SNTP_ORIGIN;
	/* Stratum zero includes rate-limit/denial replies. Do not retry them. */
	if (reply[1] == 0)
		return SNTP_REFUSED;
	if ((reply[0] >> 6) == 3)
		return SNTP_UNSYNCED;

	ntp_sec = getBe32(reply + 40);
	ntp_frac = getBe32(reply + 44);
	full_ntp = ntp_sec;
	/* NTP era 1 begins in 2036. Values below the halfway mark are interpreted
	 * as post-2036 once era 0 would predate our accepted range. */
	if (ntp_sec < 0x80000000U)
		full_ntp += 0x100000000ULL;
	if (full_ntp < SNTP_EPOCH_DELTA)
		return SNTP_DATE;
	unix_sec = full_ntp - SNTP_EPOCH_DELTA;
	if (unix_sec < UNIX_2020 || unix_sec >= UNIX_2100)
		return SNTP_DATE;
	if (received_ms < sent_ms)
		return SNTP_TIMER;
	/* Start elapsed-time tracking when the server timestamp is received.
	 * Retain milliseconds until the final one-second-resolution RTC write. */
	sample->unix_ms = unix_sec * 1000ULL + (((u64)ntp_frac * 1000ULL) >> 32);
	sample->received_ms = received_ms;
	return SNTP_OK;
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
	static const char server[] = "0.pool.ntp.org";
	SntpSample sample;
	int attempt, status = SNTP_INVALID;
	u64 now, chosen;

	if (result != NULL && result_size > 0)
		result[0] = '\0';
	if (!ensureDashboardIpNetworkReady()) {
		if (result != NULL && result_size > 0)
			snprintf(result, result_size, "%s", dashboardIpNetworkError());
		return 0;
	}
	for (attempt = 0; attempt < SNTP_ATTEMPTS; attempt++) {
		char progress[128];
		snprintf(progress, sizeof(progress), "Checking %s (attempt %d of %d)...",
		         server, attempt + 1, SNTP_ATTEMPTS);
		dashLoadingFrame(progress, attempt, SNTP_ATTEMPTS);
		status = sntpQuery(server, &sample);
		DPRINTF(" [SNTP] %s attempt %d: %s\n", server, attempt + 1, sntpStatusText(status));
		if (status == SNTP_OK || status == SNTP_REFUSED)
			break;
	}
	if (status != SNTP_OK) {
		if (result != NULL && result_size > 0)
			snprintf(result, result_size, "%s: %s.", server, sntpStatusText(status));
		return 0;
	}
	now = Timer();
	if (now < sample.received_ms) {
		if (result != NULL && result_size > 0)
			snprintf(result, result_size, "Local timer restarted. Try clock synchronization again.");
		return 0;
	}
	/* Like NTPS2, add time elapsed since receipt before writing the RTC. */
	chosen = (sample.unix_ms + now - sample.received_ms + 500ULL) / 1000ULL;
	if (!writePs2Clock(chosen)) {
		if (result != NULL && result_size > 0)
			snprintf(result, result_size, "Internet time was received, but the PS2 RTC write could not be verified.");
		return 0;
	}
	menuTitleUpdateAsync(1);
	if (result != NULL && result_size > 0)
		snprintf(result, result_size, "PS2 system clock synchronized and verified.");
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

/* Manual clock entry is available even in builds without Ethernet. The PS2
 * RTC stores the hardware time in its fixed base zone; convert the user's
 * displayed/local value through the same OSDSYS offset logic used elsewhere. */
static u8 manualToBcd(int v)
{
	return (u8)(((v / 10) << 4) | (v % 10));
}

static int manualFromBcd(u8 v)
{
	return ((v >> 4) & 15) * 10 + (v & 15);
}

static int manualLeap(int year)
{
	return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static int manualDaysInMonth(int year, int month)
{
	static const u8 days[] = {31,28,31,30,31,30,31,31,30,31,30,31};

	if (month < 1 || month > 12)
		return 0;
	return days[month - 1] + ((month == 2 && manualLeap(year)) ? 1 : 0);
}

static s64 manualSerialSeconds(int year, int month, int day, int hour, int minute, int second)
{
	s64 days = 0;
	int y, m;

	for (y = 2000; y < year; y++)
		days += manualLeap(y) ? 366 : 365;
	for (m = 1; m < month; m++)
		days += manualDaysInMonth(year, m);
	days += day - 1;
	return days * 86400 + hour * 3600 + minute * 60 + second;
}

int dashClockSetManual(int year, int month, int day, int hour, int minute, int second,
                       char *result, int result_size)
{
	sceCdCLOCK set, check;
	int raw_y = year, raw_m = month, raw_d = day;
	int raw_h = hour, raw_min = minute, raw_s = second;
	int chk_y, chk_m, chk_d, chk_h, chk_min, chk_s;
	s64 want, got, diff;

	if (result != NULL && result_size > 0)
		result[0] = '\0';
	if (year < 2000 || year > 2099 || month < 1 || month > 12 ||
	    day < 1 || day > manualDaysInMonth(year, month) ||
	    hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
	    second < 0 || second > 59) {
		if (result) snprintf(result, result_size, "Use YYYY-MM-DD HH:MM:SS with a valid date and 24-hour time.");
		return 0;
	}

	if (!menuTitleConvertTimestampFromLocalTime(&raw_y, &raw_m, &raw_d, &raw_h, &raw_min, &raw_s)) {
		if (result) snprintf(result, result_size, "Could not convert the entered local time to the PS2 RTC base.");
		return 0;
	}

	memset(&set, 0, sizeof(set));
	set.second = manualToBcd(raw_s);
	set.minute = manualToBcd(raw_min);
	set.hour = manualToBcd(raw_h);
	set.day = manualToBcd(raw_d);
	set.month = manualToBcd(raw_m);
	set.year = manualToBcd(raw_y % 100);
	if (!sceCdWriteClock(&set)) {
		if (result) snprintf(result, result_size, "The PS2 RTC write failed.");
		return 0;
	}

	memset(&check, 0, sizeof(check));
	if (!sceCdReadClock(&check) || check.stat) {
		if (result) snprintf(result, result_size, "The PS2 RTC could not be read back for verification.");
		return 0;
	}
	chk_y = 2000 + manualFromBcd(check.year);
	chk_m = manualFromBcd(check.month & 0x7f);
	chk_d = manualFromBcd(check.day);
	chk_h = manualFromBcd(check.hour);
	chk_min = manualFromBcd(check.minute);
	chk_s = manualFromBcd(check.second);
	if (!menuTitleConvertTimestampToLocalTime(&chk_y, &chk_m, &chk_d, &chk_h, &chk_min, &chk_s)) {
		if (result) snprintf(result, result_size, "The PS2 RTC read-back value was invalid.");
		return 0;
	}

	want = manualSerialSeconds(year, month, day, hour, minute, second);
	got = manualSerialSeconds(chk_y, chk_m, chk_d, chk_h, chk_min, chk_s);
	diff = got - want;
	if (diff < 0)
		diff = -diff;
	if (diff > 2) {
		if (result) snprintf(result, result_size, "The PS2 RTC write did not verify.");
		return 0;
	}

	menuTitleUpdateAsync(1);
	if (result) snprintf(result, result_size, "PS2 system clock set and verified.");
	return 1;
}
//--------------------------------------------------------------
//End of file: dash_clock_sync.c
//--------------------------------------------------------------
