//--------------------------------------------------------------
//File name:   dash_worker.c
//Background work for the dashboard: one EE thread runs file, memory
//card and driver work queued by the UI, so the cursor never waits for IO.
//
//Rules that keep the rest of wLaunchELF (which is not thread-safe) happy:
//* Exactly one job runs at a time, and while it runs the UI thread does
//  no IO of its own (device polling and disc housekeeping are skipped,
//  see dashWorkerIoBusy()). libmc and the MC status poll are therefore
//  never used from two threads at once.
//* Jobs never draw, never touch gsKit and never show a dialog. They
//  report progress with dashWorkerStatus(); the UI draws it.
//* A job's completion callback runs on the UI thread (dashWorkerPump),
//  where results are applied to the shared state.
//* Anything that hands the screen to classic code, reboots the IOP or
//  launches an ELF first waits for the queue to empty
//  (dashWorkerAwaitAll), with a cancellable progress overlay.
//
//The worker runs at a lower priority than the UI thread, so it only uses
//the time the UI spends waiting (for vsync, see dashGfxPresent, or for
//the IOP).
//--------------------------------------------------------------
#include "dash_internal.h"
#include "init.h"
#include <kernel.h>
#include <delaythread.h>

#define WORKER_STACK_SIZE (128 * 1024)
#define JOB_QUEUE 32

typedef struct
{
	DashJobFn run;
	DashJobFn done;
	void *arg;
	int flags;
	int id;
	char label[48];
} DashJob;

enum {
	CUR_IDLE = 0,
	CUR_RUNNING,
	CUR_FINISHED
};

static DashJob queue[JOB_QUEUE];
static int q_head, q_count;
static DashJob cur;
static volatile int cur_state = CUR_IDLE;
static int next_id = 1;
static int completed_id;

static int worker_ok;
static int worker_tid = -1;
static int sem_go = -1, sem_status = -1;
static u8 worker_stack[WORKER_STACK_SIZE] __attribute__((aligned(16)));

static char status_text[96];
static volatile int status_done, status_total;

/* activity indicator animation */
static float act_alpha;
static char act_label[48];
static char act_status[96];
static u64 act_visible_since;

extern void *_gp;

static void iopResetHook(int phase);
static volatile int park_req;
static volatile int parked;
static int park_depth;
static int sem_parked = -1;

//--------------------------------------------------------------
static void workerMain(void *unused)
{
	(void)unused;
	while (1) {
		WaitSema(sem_go);
		/* The UI marks this job running before waking us, preventing new
		 * status polls. Drain the previous libmc request off the UI thread. */
		filerRootMcPollIdle();
		if (cur.run != NULL)
			cur.run(cur.arg);
		cur_state = CUR_FINISHED;
	}
}

void dashWorkerInit(void)
{
	ee_sema_t sema;
	ee_thread_t th;
	ee_thread_status_t st;
	int main_prio = 64, prio;

	if (worker_ok)
		return;

	memset(&sema, 0, sizeof(sema));
	sema.init_count = 0;
	sema.max_count = 1;
	sem_go = CreateSema(&sema);
	sem_parked = CreateSema(&sema);
	sema.init_count = 1;
	sem_status = CreateSema(&sema);
	if (sem_go < 0 || sem_status < 0 || sem_parked < 0)
		return;

	if (ReferThreadStatus(GetThreadId(), &st) >= 0)
		main_prio = st.current_priority;
	if (main_prio >= 127) {
		/* the worker must be strictly lower than the UI thread */
		ChangeThreadPriority(GetThreadId(), 126);
		main_prio = 126;
	}
	prio = main_prio + 1;

	memset(&th, 0, sizeof(th));
	th.func = (void *)workerMain;
	th.stack = worker_stack;
	th.stack_size = sizeof(worker_stack);
	th.gp_reg = &_gp;
	th.initial_priority = prio;
	worker_tid = CreateThread(&th);
	if (worker_tid < 0)
		return;
	if (StartThread(worker_tid, NULL) < 0) {
		DeleteThread(worker_tid);
		worker_tid = -1;
		return;
	}
	iopResetNotify = iopResetHook;
	iopResetBlocked = dashWorkerOnThread; /* the worker only does file IO */
	worker_ok = 1;
}

int dashWorkerOnThread(void)
{
	return worker_ok && GetThreadId() == worker_tid;
}

//--------------------------------------------------------------
// IOP reboots from a background job (a device whose driver stack needs
// one, see init.c). Before the IOP goes away the UI thread must stop
// talking to it: the worker asks the UI to park and waits until it has
// reached a safe point (dashWorkerParkPoint, between frames). While parked
// the UI keeps drawing but reads no pad and plays no sound. Sound is
// restarted afterwards by dashSoundMaintain(); the pads were set up again
// by the reset code itself.
//--------------------------------------------------------------
static void iopResetHook(int phase)
{
	if (!dashWorkerOnThread())
		return; /* UI-thread resets happen inside classic code paths */
	if (phase == 0) {
		if (park_depth++ == 0) {
			park_req = 1;
			WaitSema(sem_parked);
		}
	} else if (park_depth > 0 && --park_depth == 0)
		park_req = 0;
}

void dashWorkerParkPoint(void)
{
	if (park_req && !parked) {
		parked = 1;
		SignalSema(sem_parked);
	} else if (!park_req && parked)
		parked = 0;
}

int dashIopParked(void)
{
	return parked || park_req;
}

//--------------------------------------------------------------
// Status (worker writes, UI reads)
//--------------------------------------------------------------
void dashWorkerStatus(const char *text, int done, int total)
{
	if (sem_status >= 0)
		WaitSema(sem_status);
	if (text != NULL)
		dashStrCopy(status_text, text, sizeof(status_text));
	status_done = done;
	status_total = total;
	if (sem_status >= 0)
		SignalSema(sem_status);
}

static void readStatus(char *out, int size, int *done, int *total)
{
	if (sem_status >= 0)
		WaitSema(sem_status);
	dashStrCopy(out, status_text, size);
	*done = status_done;
	*total = status_total;
	if (sem_status >= 0)
		SignalSema(sem_status);
}

//--------------------------------------------------------------
// Queue (UI thread only)
//--------------------------------------------------------------
static void dispatch(void)
{
	if (cur_state != CUR_IDLE || q_count == 0)
		return;
	cur = queue[q_head];
	q_head = (q_head + 1) % JOB_QUEUE;
	q_count--;
	dashWorkerStatus(cur.label, 0, 0);
	cur_state = CUR_RUNNING;
	SignalSema(sem_go);
}

static void finish(void)
{
	DashJob j = cur;

	cur_state = CUR_IDLE;
	completed_id = j.id;
	if (j.done != NULL)
		j.done(j.arg);
}

int dashWorkerSubmit(const char *label, int flags, DashJobFn run, DashJobFn done, void *arg)
{
	DashJob *j;

	/* Submission is nonblocking, including during drawing. Rejection
	 * transfers no ownership and invokes neither callback. */
	if (!worker_ok || q_count >= JOB_QUEUE)
		return -1;
	j = &queue[(q_head + q_count) % JOB_QUEUE];
	j->run = run;
	j->done = done;
	j->arg = arg;
	j->flags = flags;
	j->id = next_id++;
	dashStrCopy(j->label, label ? label : "", sizeof(j->label));
	q_count++;
	dispatch();
	return j->id;
}

void dashWorkerPump(void)
{
	if (!worker_ok)
		return;
	if (cur_state == CUR_FINISHED)
		finish();
	dispatch();
}

int dashWorkerBusy(void)
{
	return cur_state != CUR_IDLE || q_count > 0;
}

int dashWorkerIoBusy(void)
{
	return cur_state == CUR_RUNNING;
}

int dashWorkerQueued(int flags_mask)
{
	int i;

	if (cur_state != CUR_IDLE && (cur.flags & flags_mask))
		return 1;
	for (i = 0; i < q_count; i++)
		if (queue[(q_head + i) % JOB_QUEUE].flags & flags_mask)
			return 1;
	return 0;
}

int dashWorkerDone(int id)
{
	return id > 0 && completed_id >= id;
}

int dashWorkerLastId(void)
{
	return next_id - 1;
}

//--------------------------------------------------------------
// Waiting for results the UI cannot continue without (a theme list, a
// hand-off to classic code). The screen keeps animating, and when
// `cancelable` the user can always back out: the job keeps running in the
// background and its completion is applied as usual.
//--------------------------------------------------------------
int dashWorkerAwait(int id, const char *title, int cancelable)
{
	u64 start = Timer();
	int shown = 0, ok = 1;

	if (id <= 0)
		return 0;

	while (!dashWorkerDone(id)) {
		DashInput in;

		dashWorkerPump();
		if (dashWorkerDone(id))
			break;
		dashInputPoll(&in);
		if (cancelable && (in.pressed & DASH_IN_BACK)) {
			ok = 0;
			break;
		}
		/* quick jobs finish without any overlay flashing up */
		if (!shown && Timer() - start >= 150) {
			dashLoadingBegin(title);
			shown = 1;
		}
		if (shown) {
			char s[96];
			int done, total;
			readStatus(s, sizeof(s), &done, &total);
			dashLoadingSetCancel(cancelable);
			dashLoadingFrame(s, done, total);
		} else {
			/* let the worker run while we wait */
			dashVisualUpdate();
			dashRenderScene();
			dashGfxPresent();
		}
	}
	if (shown)
		dashLoadingEnd();
	return ok;
}

int dashWorkerAwaitAll(const char *title)
{
	if (!dashWorkerBusy())
		return 1;
	return dashWorkerAwait(dashWorkerLastId(), title, 1);
}

//--------------------------------------------------------------
// Activity indicator: a small pill above the button hints while visible
// work runs. Drawn by dashRenderScene on every screen.
//--------------------------------------------------------------
static int visibleWork(void)
{
	return dashWorkerQueued(DASH_JOB_VISIBLE);
}

void dashDrawActivity(void)
{
	int active = visibleWork();
	u64 now = Timer();
	float target, w, x, y = 364.0f;
	char s[96];
	int done, total;

	if (active) {
		if (act_alpha <= 0.0f)
			act_visible_since = now;
		if (cur_state != CUR_IDLE && (cur.flags & DASH_JOB_VISIBLE)) {
			dashStrCopy(act_label, cur.label, sizeof(act_label));
			readStatus(s, sizeof(s), &done, &total);
			if (total > 0)
				snprintf(act_status, sizeof(act_status), "%d/%d", done + 1 > total ? total : done + 1, total);
			else
				act_status[0] = '\0';
		}
	}
	/* stay up for at least half a second so short saves are still seen */
	target = (active || now - act_visible_since < 500) ? 1.0f : 0.0f;
	act_alpha = dashApproach(act_alpha, target, 10.0f, dashFrameDelta());
	if (act_alpha < 0.02f) {
		act_alpha = 0.0f;
		return;
	}
	if (act_label[0] == '\0')
		return;

	w = 44.0f + dashTextWidth(act_label, DASH_FS_SMALL) + (act_status[0] ? dashTextWidth(act_status, DASH_FS_CAPTION) + 10.0f : 0.0f);
	x = DASH_SAFE_R - w;
	dashGfxPushAlpha(dashEaseOutCubic(act_alpha));
	dashShadow(x, y, w, 26, 8, 0.5f);
	dashPanel(x, y, w, 26, 13, DC(DASH_C_PANEL_HI.r, DASH_C_PANEL_HI.g, DASH_C_PANEL_HI.b, 225));
	if (DASH_C_BORDER.a)
		dashPanelBorder(x, y, w, 26, 13, DASH_C_BORDER);
	dashGfxBlend(DASH_BLEND_ADD);
	dashDrawSpinner(x + 16, y + 13, 6.5f, DASH_C_ACCENT);
	dashGfxBlend(DASH_BLEND_NORMAL);
	{
		float tx = dashText(x + 32, y + 6, act_label, DASH_FS_SMALL, DASH_C_TEXT);
		if (act_status[0])
			dashText(tx + 8, y + 7.5f, act_status, DASH_FS_CAPTION, DASH_C_TEXT3);
	}
	dashGfxPopAlpha();
}
//--------------------------------------------------------------
//End of file: dash_worker.c
//--------------------------------------------------------------
