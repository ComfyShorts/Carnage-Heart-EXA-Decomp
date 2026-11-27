/* PSP kernel imports with real implementations: threads, callbacks, mutexes, partition
 * memory, timing, module/SDK bookkeeping, and the small system queries the game makes
 * while starting up (system parameters, the clock, the impose language setting).
 *
 * Model:
 *  - Each PSP thread is an SDL thread. The PSP scheduler is priority-based and
 *    non-preemptive per core; here threads run truly in parallel, so the game's own
 *    locks (mutexes, sceKernelCpuSuspendIntr) are what keep shared state safe.
 *  - sceKernelExitThread / ExitDeleteThread / ExitGame unwind to the thread's entry
 *    wrapper with longjmp. Game code is plain C, so nothing is skipped that matters.
 *  - Callbacks run only on the thread that created them, during a *CB wait, as on the
 *    PSP. psp_kernel_request_exit() notifies the registered exit callback (the HOME
 *    menu's "quit game").
 *  - Kernel objects live in one UID table. */
#include "psp_imports.h"
#include "psp_shim.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

/* ---- object table ---- */

typedef enum ObjKind { OBJ_FREE, OBJ_THREAD, OBJ_CALLBACK, OBJ_MUTEX, OBJ_BLOCK } ObjKind;

typedef struct PspThread {
    char name[32];
    SceKernelThreadEntry entry;
    int priority;
    int stack_size;
    uint32_t attr;
    SDL_Thread *sdl;
    void *args;
    SceSize arglen;
    jmp_buf exit_jmp;
    int exit_status;
    SDL_AtomicInt running; /* 1 from start until the entry returns or exits */
    bool started;
    bool delete_on_exit;
} PspThread;

typedef struct PspCallback {
    char name[32];
    SceKernelCallbackFunction func;
    void *arg;
    SceUID owner;    /* thread that created it; only that thread runs it */
    int notify_count;
    int notify_arg;
} PspCallback;

typedef struct PspMutex {
    char name[32];
    uint32_t attr;
    SDL_Mutex *lock;
    SDL_Condition *cond;
    SceUID owner;
    int count;
} PspMutex;

typedef struct PspBlock {
    char name[32];
    void *raw;  /* what malloc returned */
    void *head; /* aligned start handed to the game */
    SceSize size;
} PspBlock;

typedef struct Obj {
    ObjKind kind;
    void *p;
} Obj;

#define MAX_OBJS 512
#define UID_BASE 0x1001
#define PSP_MUTEX_ATTR_RECURSIVE 0x200

#define SCE_KERNEL_ERROR_ILLEGAL_PRIORITY 0x80020193
#define SCE_KERNEL_ERROR_NOT_DORMANT      0x800201A4
#define SCE_KERNEL_ERROR_UNKNOWN_THID     0x80020198
#define SCE_KERNEL_ERROR_UNKNOWN_CBID     0x800201A2
#define SCE_KERNEL_ERROR_UNKNOWN_MUTEXID  0x800201C3
#define SCE_KERNEL_ERROR_UNKNOWN_UID      0x800200CB
#define SCE_KERNEL_ERROR_MUTEX_LOCK_OVF   0x800201C5
#define SCE_KERNEL_ERROR_MUTEX_UNLOCKED   0x800201C7
#define SCE_KERNEL_ERROR_MUTEX_RECURSIVE  0x800201C4
#define SCE_KERNEL_ERROR_ILLEGAL_COUNT    0x800201BD
#define SCE_KERNEL_ERROR_NO_MEMORY        0x80020190
#define SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT 0x800200D2
#define SCE_UTILITY_ERROR_INVALID_ID      0x80110103

static Obj g_objs[MAX_OBJS];
static SDL_Mutex *g_kernel_lock; /* guards g_objs and the globals below */
static SDL_Mutex *g_intr_lock;   /* sceKernelCpuSuspendIntr: one recursive "interrupts off" lock */
static SDL_AtomicInt g_threads_alive;
static SDL_AtomicInt g_exit_game;
static SceUID g_exit_cb = -1;
static SceUID g_power_cb[16];
static uint32_t g_sdk_version, g_compiler_version;
static int g_impose_language = 1, g_impose_button = 1;
static uint64_t g_start_ns;

static _Thread_local PspThread *t_self;
static _Thread_local SceUID t_self_uid;

static void kernel_init_once(void)
{
    static SDL_InitState state;
    if (!SDL_ShouldInit(&state))
        return;
    g_kernel_lock = SDL_CreateMutex();
    g_intr_lock = SDL_CreateMutex(); /* SDL mutexes are recursive */
    g_start_ns = SDL_GetTicksNS();
    for (int i = 0; i < 16; i++)
        g_power_cb[i] = -1;
    SDL_SetInitialized(&state, true);
}

static void klock(void)
{
    kernel_init_once();
    SDL_LockMutex(g_kernel_lock);
}

static void kunlock(void)
{
    SDL_UnlockMutex(g_kernel_lock);
}

/* Caller holds the kernel lock. */
static SceUID obj_add(ObjKind kind, void *p)
{
    for (int i = 0; i < MAX_OBJS; i++)
        if (g_objs[i].kind == OBJ_FREE) {
            g_objs[i] = (Obj){kind, p};
            return UID_BASE + i;
        }
    return SCE_KERNEL_ERROR_NO_MEMORY;
}

/* Caller holds the kernel lock. */
static void *obj_get(SceUID uid, ObjKind kind)
{
    int i = uid - UID_BASE;
    if (i < 0 || i >= MAX_OBJS || g_objs[i].kind != kind)
        return NULL;
    return g_objs[i].p;
}

static void obj_remove(SceUID uid)
{
    int i = uid - UID_BASE;
    if (i >= 0 && i < MAX_OBJS)
        g_objs[i] = (Obj){OBJ_FREE, NULL};
}

static void copy_name(char *dst, const char *src)
{
    snprintf(dst, 32, "%s", src ? src : "");
}

/* ---- callbacks ---- */

/* Runs the current thread's notified callbacks. Returns how many ran. */
static int run_callbacks(void)
{
    int ran = 0;
    for (;;) {
        PspCallback *cb = NULL, run;
        SceUID cbid = -1;
        klock();
        for (int i = 0; i < MAX_OBJS && !cb; i++)
            if (g_objs[i].kind == OBJ_CALLBACK) {
                PspCallback *c = g_objs[i].p;
                if (c->notify_count > 0 && c->owner == t_self_uid) {
                    cb = c;
                    cbid = UID_BASE + i;
                }
            }
        if (cb) {
            run = *cb;
            cb->notify_count = 0;
            cb->notify_arg = 0;
        }
        kunlock();
        if (!cb)
            return ran;
        psp_trace("(callback)", 0, "\"%s\" uid %d, count %d, arg %d", run.name, (int)cbid,
                  run.notify_count, run.notify_arg);
        if (run.func)
            run.func(run.notify_count, run.notify_arg, run.arg);
        ran++;
    }
}

int psp_kernel_run_callbacks(void)
{
    return run_callbacks();
}

static void notify_callback(SceUID cbid, int arg)
{
    klock();
    PspCallback *cb = obj_get(cbid, OBJ_CALLBACK);
    if (cb) {
        cb->notify_count++;
        cb->notify_arg = arg;
    }
    kunlock();
}

void psp_kernel_request_exit(void)
{
    kernel_init_once();
    klock();
    SceUID cb = g_exit_cb;
    kunlock();
    if (cb >= 0)
        notify_callback(cb, 0);
    else
        SDL_SetAtomicInt(&g_exit_game, 1);
}

bool psp_kernel_exit_called(void)
{
    return SDL_GetAtomicInt(&g_exit_game) != 0;
}

int psp_kernel_threads_alive(void)
{
    return SDL_GetAtomicInt(&g_threads_alive);
}

SceUID sceKernelCreateCallback(const char *name, SceKernelCallbackFunction func, void *arg)
{
    PspCallback *cb = calloc(1, sizeof *cb);
    copy_name(cb->name, name);
    cb->func = func;
    cb->arg = arg;
    cb->owner = t_self_uid;
    klock();
    SceUID r = obj_add(OBJ_CALLBACK, cb);
    kunlock();
    if (r < 0)
        free(cb);
    PSP_TRACE(r, "\"%s\", %p, %p", name ? name : "", (void *)func, arg);
    return r;
}

int32_t sceKernelDeleteCallback(SceUID cbid)
{
    klock();
    PspCallback *cb = obj_get(cbid, OBJ_CALLBACK);
    if (cb)
        obj_remove(cbid);
    kunlock();
    free(cb);
    int32_t r = cb ? 0 : (int32_t)SCE_KERNEL_ERROR_UNKNOWN_CBID;
    PSP_TRACE(r, "%d", (int)cbid);
    return r;
}

int32_t sceKernelCheckCallback(void)
{
    int32_t r = run_callbacks() > 0;
    PSP_TRACE(r, NULL);
    return r;
}

int32_t sceKernelRegisterExitCallback(SceUID cbid)
{
    klock();
    int32_t r = obj_get(cbid, OBJ_CALLBACK) ? 0 : (int32_t)SCE_KERNEL_ERROR_UNKNOWN_CBID;
    if (r == 0)
        g_exit_cb = cbid;
    kunlock();
    PSP_TRACE(r, "%d", (int)cbid);
    return r;
}

int32_t scePowerRegisterCallback(int slot, SceUID cbid)
{
    /* Power events (suspend, resume, battery) never happen on the PC. */
    int32_t r = SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
    klock();
    if (!obj_get(cbid, OBJ_CALLBACK))
        r = SCE_KERNEL_ERROR_UNKNOWN_CBID;
    else if (slot == -1) {
        for (int i = 0; i < 16; i++)
            if (g_power_cb[i] < 0) {
                g_power_cb[i] = cbid;
                r = i;
                break;
            }
    } else if (slot >= 0 && slot < 16 && g_power_cb[slot] < 0) {
        g_power_cb[slot] = cbid;
        r = slot;
    }
    kunlock();
    PSP_TRACE(r, "%d, %d", slot, (int)cbid);
    return r;
}

int32_t scePowerUnregisterCallback(int slot)
{
    int32_t r = SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
    klock();
    if (slot >= 0 && slot < 16 && g_power_cb[slot] >= 0) {
        g_power_cb[slot] = -1;
        r = 0;
    }
    kunlock();
    PSP_TRACE(r, "%d", slot);
    return r;
}

/* ---- threads ---- */

static int thread_main(void *p)
{
    PspThread *t = p;
    t_self = t;
    klock();
    for (int i = 0; i < MAX_OBJS; i++)
        if (g_objs[i].p == t)
            t_self_uid = UID_BASE + i;
    kunlock();
    if (setjmp(t->exit_jmp) == 0)
        t->exit_status = t->entry(t->arglen, t->args);
    SDL_Log("[kernel] thread \"%s\" ended, status 0x%X", t->name, (unsigned)t->exit_status);
    SDL_SetAtomicInt(&t->running, 0);
    SDL_AddAtomicInt(&g_threads_alive, -1);
    return t->exit_status;
}

SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int priority, int stack_size,
                             uint32_t attr, void *option)
{
    SceUID r;
    if (priority < 0x08 || priority > 0x77) {
        r = SCE_KERNEL_ERROR_ILLEGAL_PRIORITY;
    } else {
        PspThread *t = calloc(1, sizeof *t);
        copy_name(t->name, name);
        t->entry = entry;
        t->priority = priority;
        t->stack_size = stack_size;
        t->attr = attr;
        klock();
        r = obj_add(OBJ_THREAD, t);
        kunlock();
        if (r < 0)
            free(t);
    }
    PSP_TRACE(r, "\"%s\", %p, 0x%X, 0x%X, 0x%X, %p", name ? name : "", (void *)entry, priority,
              stack_size, (unsigned)attr, option);
    return r;
}

int32_t sceKernelStartThread(SceUID thid, SceSize arglen, void *argp)
{
    klock();
    PspThread *t = obj_get(thid, OBJ_THREAD);
    int32_t r = !t ? (int32_t)SCE_KERNEL_ERROR_UNKNOWN_THID
              : t->started ? (int32_t)SCE_KERNEL_ERROR_NOT_DORMANT : 0;
    if (r == 0) {
        /* The PSP copies the argument block onto the new thread's stack. */
        t->arglen = arglen;
        t->args = argp && arglen ? malloc(arglen) : NULL;
        if (t->args)
            memcpy(t->args, argp, arglen);
        t->started = true;
        SDL_SetAtomicInt(&t->running, 1);
        SDL_AddAtomicInt(&g_threads_alive, 1);
        /* Host code needs more stack than the PSP's; never go below 1 MiB. */
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)thread_main);
        SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, t->name);
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, t);
        SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER,
                              t->stack_size > 0x100000 ? t->stack_size : 0x100000);
        t->sdl = SDL_CreateThreadWithProperties(props);
        SDL_DestroyProperties(props);
        if (!t->sdl) {
            SDL_Log("[kernel] can't start thread \"%s\": %s", t->name, SDL_GetError());
            SDL_SetAtomicInt(&t->running, 0);
            SDL_AddAtomicInt(&g_threads_alive, -1);
            r = SCE_KERNEL_ERROR_NO_MEMORY;
        }
    }
    kunlock();
    PSP_TRACE(r, "%d, %u, %p", (int)thid, (unsigned)arglen, argp);
    return r;
}

static void exit_current_thread(int status, bool delete_self)
{
    PspThread *t = t_self;
    if (!t) {
        /* Not a PSP thread (the host main thread): nothing to unwind to. */
        SDL_Log("[kernel] thread exit (status 0x%X) called outside a PSP thread", (unsigned)status);
        return;
    }
    t->exit_status = status;
    t->delete_on_exit = delete_self;
    longjmp(t->exit_jmp, 1);
}

int32_t sceKernelExitThread(int status)
{
    PSP_TRACE(0, "0x%X", (unsigned)status);
    exit_current_thread(status, false);
    return 0;
}

int32_t sceKernelExitDeleteThread(int status)
{
    PSP_TRACE(0, "0x%X", (unsigned)status);
    exit_current_thread(status, true);
    return 0;
}

int32_t sceKernelDeleteThread(SceUID thid)
{
    klock();
    PspThread *t = obj_get(thid, OBJ_THREAD);
    int32_t r = !t ? (int32_t)SCE_KERNEL_ERROR_UNKNOWN_THID
              : SDL_GetAtomicInt(&t->running) ? (int32_t)SCE_KERNEL_ERROR_NOT_DORMANT : 0;
    SDL_Thread *join = NULL;
    if (r == 0) {
        obj_remove(thid);
        join = t->sdl;
    }
    kunlock();
    if (r == 0) {
        if (join)
            SDL_WaitThread(join, NULL);
        free(t->args);
        free(t);
    }
    PSP_TRACE(r, "%d", (int)thid);
    return r;
}

int32_t sceKernelWaitThreadEnd(SceUID thid, uint32_t *timeout)
{
    uint64_t deadline = timeout ? SDL_GetTicksNS() + (uint64_t)*timeout * 1000 : 0;
    int32_t r;
    for (;;) {
        klock();
        PspThread *t = obj_get(thid, OBJ_THREAD);
        r = !t ? (int32_t)SCE_KERNEL_ERROR_UNKNOWN_THID : SDL_GetAtomicInt(&t->running) ? 1 : t->exit_status;
        kunlock();
        if (r != 1 || !t)
            break;
        if (timeout && SDL_GetTicksNS() >= deadline) {
            r = SCE_KERNEL_ERROR_WAIT_TIMEOUT;
            break;
        }
        SDL_DelayNS(500000);
    }
    PSP_TRACE(r, "%d", (int)thid);
    return r;
}

SceUID sceKernelGetThreadId(void)
{
    SceUID r = t_self ? t_self_uid : (SceUID)SCE_KERNEL_ERROR_UNKNOWN_THID;
    PSP_TRACE(r, NULL);
    return r;
}

int32_t sceKernelGetThreadExitStatus(SceUID thid)
{
    klock();
    PspThread *t = obj_get(thid, OBJ_THREAD);
    int32_t r = !t ? (int32_t)SCE_KERNEL_ERROR_UNKNOWN_THID
              : SDL_GetAtomicInt(&t->running) || !t->started ? (int32_t)SCE_KERNEL_ERROR_NOT_DORMANT
              : t->exit_status;
    kunlock();
    PSP_TRACE(r, "%d", (int)thid);
    return r;
}

int32_t sceKernelGetThreadCurrentPriority(void)
{
    int32_t r = t_self ? t_self->priority : 0x20;
    PSP_TRACE(r, NULL);
    return r;
}

int32_t sceKernelChangeThreadPriority(SceUID thid, int priority)
{
    /* Priorities are recorded but not enforced: host threads all run at once. */
    klock();
    PspThread *t = thid == 0 ? t_self : obj_get(thid, OBJ_THREAD);
    int32_t r = !t ? (int32_t)SCE_KERNEL_ERROR_UNKNOWN_THID : 0;
    if (t && priority != 0)
        t->priority = priority;
    kunlock();
    PSP_TRACE(r, "%d, 0x%X", (int)thid, priority);
    return r;
}

int32_t sceKernelChangeCurrentThreadAttr(uint32_t clear_attr, uint32_t set_attr)
{
    /* 0x4000 is PSP_THREAD_ATTR_VFPU; nothing to do on the host. */
    if (t_self)
        t_self->attr = (t_self->attr & ~clear_attr) | set_attr;
    PSP_TRACE(0, "0x%X, 0x%X", (unsigned)clear_attr, (unsigned)set_attr);
    return 0;
}

int32_t sceKernelDelayThread(uint32_t usec)
{
    PSP_TRACE(0, "%u", (unsigned)usec);
    SDL_DelayNS((uint64_t)usec * 1000);
    return 0;
}

int32_t sceKernelDelayThreadCB(uint32_t usec)
{
    PSP_TRACE(0, "%u", (unsigned)usec);
    run_callbacks();
    SDL_DelayNS((uint64_t)usec * 1000);
    run_callbacks();
    return 0;
}

/* ---- mutexes ---- */

SceUID sceKernelCreateMutex(const char *name, uint32_t attr, int init_count, void *option)
{
    SceUID r;
    if (init_count < 0 || (init_count > 1 && !(attr & PSP_MUTEX_ATTR_RECURSIVE))) {
        r = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
    } else {
        PspMutex *m = calloc(1, sizeof *m);
        copy_name(m->name, name);
        m->attr = attr;
        m->lock = SDL_CreateMutex();
        m->cond = SDL_CreateCondition();
        m->count = init_count;
        m->owner = init_count ? t_self_uid : 0;
        klock();
        r = obj_add(OBJ_MUTEX, m);
        kunlock();
        if (r < 0) {
            SDL_DestroyCondition(m->cond);
            SDL_DestroyMutex(m->lock);
            free(m);
        }
    }
    PSP_TRACE(r, "\"%s\", 0x%X, %d, %p", name ? name : "", (unsigned)attr, init_count, option);
    return r;
}

int32_t sceKernelDeleteMutex(SceUID id)
{
    klock();
    PspMutex *m = obj_get(id, OBJ_MUTEX);
    if (m)
        obj_remove(id);
    kunlock();
    if (m) {
        SDL_DestroyCondition(m->cond);
        SDL_DestroyMutex(m->lock);
        free(m);
    }
    int32_t r = m ? 0 : (int32_t)SCE_KERNEL_ERROR_UNKNOWN_MUTEXID;
    PSP_TRACE(r, "%d", (int)id);
    return r;
}

int32_t sceKernelLockMutexCB(SceUID id, int count, uint32_t *timeout)
{
    klock();
    PspMutex *m = obj_get(id, OBJ_MUTEX);
    kunlock();
    int32_t r = 0;
    SceUID me = t_self_uid;
    if (!m)
        r = SCE_KERNEL_ERROR_UNKNOWN_MUTEXID;
    else if (count <= 0 || (count > 1 && !(m->attr & PSP_MUTEX_ATTR_RECURSIVE)))
        r = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
    else {
        run_callbacks();
        uint64_t deadline = timeout ? SDL_GetTicksNS() + (uint64_t)*timeout * 1000 : 0;
        SDL_LockMutex(m->lock);
        for (;;) {
            if (m->count == 0) {
                m->owner = me;
                m->count = count;
                break;
            }
            if (m->owner == me) {
                if (!(m->attr & PSP_MUTEX_ATTR_RECURSIVE))
                    r = SCE_KERNEL_ERROR_MUTEX_RECURSIVE;
                else
                    m->count += count;
                break;
            }
            if (timeout) {
                uint64_t now = SDL_GetTicksNS();
                if (now >= deadline) {
                    *timeout = 0;
                    r = SCE_KERNEL_ERROR_WAIT_TIMEOUT;
                    break;
                }
                SDL_WaitConditionTimeout(m->cond, m->lock, (Sint32)((deadline - now) / 1000000 + 1));
            } else {
                SDL_WaitCondition(m->cond, m->lock);
            }
        }
        SDL_UnlockMutex(m->lock);
    }
    PSP_TRACE(r, "%d, %d", (int)id, count);
    return r;
}

int32_t sceKernelUnlockMutex(SceUID id, int count)
{
    klock();
    PspMutex *m = obj_get(id, OBJ_MUTEX);
    kunlock();
    int32_t r = 0;
    if (!m)
        r = SCE_KERNEL_ERROR_UNKNOWN_MUTEXID;
    else if (count <= 0)
        r = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
    else {
        SDL_LockMutex(m->lock);
        if (m->count == 0 || m->owner != t_self_uid)
            r = SCE_KERNEL_ERROR_MUTEX_UNLOCKED;
        else if (count > m->count)
            r = SCE_KERNEL_ERROR_ILLEGAL_COUNT;
        else if ((m->count -= count) == 0) {
            m->owner = 0;
            SDL_BroadcastCondition(m->cond);
        }
        SDL_UnlockMutex(m->lock);
    }
    PSP_TRACE(r, "%d, %d", (int)id, count);
    return r;
}

/* ---- interrupts and caches ---- */

uint32_t sceKernelCpuSuspendIntr(void)
{
    kernel_init_once();
    SDL_LockMutex(g_intr_lock);
    PSP_TRACE(1, NULL);
    return 1;
}

void sceKernelCpuResumeIntr(uint32_t flags)
{
    PSP_TRACE(0, "%u", (unsigned)flags);
    SDL_UnlockMutex(g_intr_lock);
}

void sceKernelCpuResumeIntrWithSync(uint32_t flags)
{
    PSP_TRACE(0, "%u", (unsigned)flags);
    SDL_UnlockMutex(g_intr_lock);
}

/* The host has coherent caches, so these are no-ops. */
void sceKernelDcacheWritebackAll(void)
{
    PSP_TRACE(0, NULL);
}

void sceKernelDcacheWritebackInvalidateAll(void)
{
    PSP_TRACE(0, NULL);
}

void sceKernelDcacheWritebackRange(const void *p, unsigned int size)
{
    PSP_TRACE(0, "%p, %u", p, size);
}

/* ---- time ---- */

SceInt64 sceKernelGetSystemTimeWide(void)
{
    kernel_init_once();
    SceInt64 r = (SceInt64)((SDL_GetTicksNS() - g_start_ns) / 1000);
    PSP_TRACE(r, NULL);
    return r;
}

uint32_t sceKernelGetSystemTimeLow(void)
{
    kernel_init_once();
    uint32_t r = (uint32_t)((SDL_GetTicksNS() - g_start_ns) / 1000);
    PSP_TRACE(r, NULL);
    return r;
}

int32_t sceRtcGetCurrentClock(ScePspDateTime *out, int tz_minutes)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t t = ts.tv_sec + (time_t)tz_minutes * 60;
    struct tm tm;
    gmtime_r(&t, &tm);
    if (out) {
        out->year = (uint16_t)(tm.tm_year + 1900);
        out->month = (uint16_t)(tm.tm_mon + 1);
        out->day = (uint16_t)tm.tm_mday;
        out->hour = (uint16_t)tm.tm_hour;
        out->minute = (uint16_t)tm.tm_min;
        out->second = (uint16_t)tm.tm_sec;
        out->microsecond = (uint32_t)(ts.tv_nsec / 1000);
    }
    PSP_TRACE(0, "%p, %d -> %04d-%02d-%02d %02d:%02d:%02d", (void *)out, tz_minutes, tm.tm_year + 1900,
              tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return 0;
}

/* ---- partition memory ---- */

SceUID sceKernelAllocPartitionMemory(SceUID partition, const char *name, int type, SceSize size, void *addr)
{
    /* Types 3/4 (low/high aligned) take the alignment in addr; 2 asks for a fixed
     * address, which can't be honoured on the host. */
    SceUID r;
    size_t align = 256;
    if (type == 3 || type == 4)
        align = (uintptr_t)addr;
    if (type < 0 || type > 4 || type == 2 || size == 0 || align == 0 || (align & (align - 1))) {
        r = SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT;
    } else {
        if (align < 256)
            align = 256; /* the PSP hands out 256-byte units */
        PspBlock *b = calloc(1, sizeof *b);
        copy_name(b->name, name);
        b->size = size;
        b->raw = malloc(size + align);
        if (!b->raw) {
            free(b);
            r = SCE_KERNEL_ERROR_NO_MEMORY;
        } else {
            b->head = (void *)(((uintptr_t)b->raw + align - 1) & ~(uintptr_t)(align - 1));
            klock();
            r = obj_add(OBJ_BLOCK, b);
            kunlock();
            if (r < 0) {
                free(b->raw);
                free(b);
            }
        }
    }
    PSP_TRACE(r, "%d, \"%s\", %d, 0x%X, %p", (int)partition, name ? name : "", type, (unsigned)size, addr);
    return r;
}

int32_t sceKernelFreePartitionMemory(SceUID blockid)
{
    klock();
    PspBlock *b = obj_get(blockid, OBJ_BLOCK);
    if (b)
        obj_remove(blockid);
    kunlock();
    if (b) {
        free(b->raw);
        free(b);
    }
    int32_t r = b ? 0 : (int32_t)SCE_KERNEL_ERROR_UNKNOWN_UID;
    PSP_TRACE(r, "%d", (int)blockid);
    return r;
}

void *sceKernelGetBlockHeadAddr(SceUID blockid)
{
    klock();
    PspBlock *b = obj_get(blockid, OBJ_BLOCK);
    kunlock();
    void *r = b ? b->head : NULL;
    PSP_TRACE_PTR(r, "%d", (int)blockid);
    return r;
}

/* ---- modules, SDK, process ---- */

#define GAME_MODULE_ID 0x0F00001

int32_t sceKernelSetCompiledSdkVersion606(uint32_t version)
{
    g_sdk_version = version;
    PSP_TRACE(0, "0x%08X", (unsigned)version);
    return 0;
}

int32_t sceKernelSetCompilerVersion(uint32_t version)
{
    g_compiler_version = version;
    PSP_TRACE(0, "0x%08X", (unsigned)version);
    return 0;
}

SceUID sceKernelGetModuleId(void)
{
    PSP_TRACE(GAME_MODULE_ID, NULL);
    return GAME_MODULE_ID;
}

SceUID sceKernelGetModuleIdByAddress(const void *addr)
{
    PSP_TRACE(GAME_MODULE_ID, "%p", addr);
    return GAME_MODULE_ID;
}

int32_t sceUtilityLoadModule(int module)
{
    /* The system modules (codecs, net, ...) are part of the port; nothing to load. */
    PSP_TRACE(0, "0x%X", module);
    return 0;
}

int32_t sceUtilityUnloadModule(int module)
{
    PSP_TRACE(0, "0x%X", module);
    return 0;
}

int32_t sceKernelExitGame(void)
{
    PSP_TRACE(0, NULL);
    SDL_SetAtomicInt(&g_exit_game, 1);
    exit_current_thread(0, true);
    return 0;
}

SceUID sceKernelStdin(void)
{
    PSP_TRACE(0, NULL);
    return 0;
}

SceUID sceKernelStdout(void)
{
    PSP_TRACE(1, NULL);
    return 1;
}

SceUID sceKernelStderr(void)
{
    PSP_TRACE(2, NULL);
    return 2;
}

int32_t sceKernelPrintf(const char *format, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof buf, format ? format : "", ap);
    va_end(ap);
    PSP_TRACE(0, "\"%.60s\"", format ? format : "");
    fputs(buf, stdout);
    fflush(stdout);
    return n;
}

/* ---- system settings ---- */

int32_t sceImposeSetLanguageMode(int language, int button)
{
    g_impose_language = language;
    g_impose_button = button;
    PSP_TRACE(0, "%d, %d", language, button);
    return 0;
}

int32_t sceUtilityGetSystemParamInt(int id, int *value)
{
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    int32_t r = 0, v = 0;
    switch (id) {
    case 2: v = 0; break;                                        /* ad hoc channel: auto */
    case 3: v = 0; break;                                        /* WLAN power save: off */
    case 4: v = 0; break;                                        /* date format: YYYY/MM/DD */
    case 5: v = 0; break;                                        /* time format: 24-hour */
    case 6: v = (int)(lt.tm_gmtoff / 60) - (lt.tm_isdst > 0 ? 60 : 0); break; /* time zone, minutes */
    case 7: v = lt.tm_isdst > 0; break;                         /* daylight saving */
    case 8: v = g_impose_language; break;                       /* language */
    case 9: v = g_impose_button; break;                         /* 1: cross confirms */
    default: r = SCE_UTILITY_ERROR_INVALID_ID; break;
    }
    if (r == 0 && value)
        *value = v;
    PSP_TRACE(r, "%d -> %d", id, v);
    return r;
}
