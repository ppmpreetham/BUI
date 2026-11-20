/* Minimal thread-primitive implementation backing BLI_threads.h.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdlib.h>

#include "BLI_threads.h"
#include "BLI_listbase.h"
#include "DNA_listBase.h"
#include "MEM_guardedalloc.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

void BLI_mutex_init(ThreadMutex *mutex)
{
#ifdef _WIN32
  CRITICAL_SECTION *cs = malloc(sizeof(CRITICAL_SECTION));
  InitializeCriticalSection(cs);
  mutex->impl = cs;
#else
  pthread_mutex_init(&mutex->mutex, NULL);
#endif
}

void BLI_mutex_lock(ThreadMutex *mutex)
{
#ifdef _WIN32
  EnterCriticalSection((CRITICAL_SECTION *)mutex->impl);
#else
  pthread_mutex_lock(&mutex->mutex);
#endif
}

void BLI_mutex_unlock(ThreadMutex *mutex)
{
#ifdef _WIN32
  LeaveCriticalSection((CRITICAL_SECTION *)mutex->impl);
#else
  pthread_mutex_unlock(&mutex->mutex);
#endif
}

void BLI_mutex_destroy(ThreadMutex *mutex)
{
#ifdef _WIN32
  CRITICAL_SECTION *cs = (CRITICAL_SECTION *)mutex->impl;
  if (cs) {
    DeleteCriticalSection(cs);
    free(cs);
    mutex->impl = NULL;
  }
#else
  pthread_mutex_destroy(&mutex->mutex);
#endif
}

void BLI_condition_init(ThreadCondition *cond)
{
#ifdef _WIN32
  InitializeConditionVariable((CONDITION_VARIABLE *)&cond->impl);
#else
  pthread_cond_init(cond, NULL);
#endif
}

void BLI_condition_wait(ThreadCondition *cond, ThreadMutex *mutex)
{
#ifdef _WIN32
  SleepConditionVariableCS(
      (CONDITION_VARIABLE *)&cond->impl, (CRITICAL_SECTION *)mutex->impl, INFINITE);
#else
  pthread_cond_wait(cond, &mutex->mutex);
#endif
}

void BLI_condition_notify_one(ThreadCondition *cond)
{
#ifdef _WIN32
  WakeConditionVariable((CONDITION_VARIABLE *)&cond->impl);
#else
  pthread_cond_signal(cond);
#endif
}

void BLI_condition_notify_all(ThreadCondition *cond)
{
#ifdef _WIN32
  WakeAllConditionVariable((CONDITION_VARIABLE *)&cond->impl);
#else
  pthread_cond_broadcast(cond);
#endif
}

void BLI_condition_end(ThreadCondition *cond)
{
#ifdef _WIN32
  (void)cond; /* Win32 condition variables need no cleanup */
#else
  pthread_cond_destroy(cond);
#endif
}

/* -- thread pool (Blender's BLI_threadpool_*, BLI_threads.cc) -------------------- */

typedef struct ThreadSlot {
  struct ThreadSlot *next, *prev;
  void *(*do_thread)(void *);
  void *callerdata;
#ifdef _WIN32
  HANDLE handle;
#else
  pthread_t pthread;
#endif
  int avail;
} ThreadSlot;

#ifdef _WIN32
static DWORD WINAPI thread_main(LPVOID arg)
{
  ThreadSlot *slot = (ThreadSlot *)arg;
  slot->do_thread(slot->callerdata);
  return 0;
}
#endif

void BLI_threadpool_init(ListBase *threadbase, void *(*do_thread)(void *), int tot)
{
  BLI_listbase_clear(threadbase);
  for (int a = 0; a < tot; a++) {
    ThreadSlot *slot = (ThreadSlot *)MEM_callocN(sizeof(ThreadSlot), "threadslot");
    slot->do_thread = do_thread;
    slot->avail = 1;
    BLI_addtail(threadbase, slot);
  }
}

void BLI_threadpool_insert(ListBase *threadbase, void *callerdata)
{
  for (ThreadSlot *slot = (ThreadSlot *)threadbase->first; slot; slot = slot->next) {
    if (slot->avail) {
      slot->avail = 0;
      slot->callerdata = callerdata;
#ifdef _WIN32
      slot->handle = CreateThread(NULL, 0, thread_main, slot, 0, NULL);
      slot->avail = slot->handle == NULL;
#else
      slot->avail = pthread_create(&slot->pthread, NULL, slot->do_thread, callerdata) != 0;
#endif
      return;
    }
  }
}

void BLI_threadpool_end(ListBase *threadbase)
{
  for (ThreadSlot *slot = (ThreadSlot *)threadbase->first; slot; slot = slot->next) {
    if (!slot->avail) {
#ifdef _WIN32
      WaitForSingleObject(slot->handle, INFINITE);
      CloseHandle(slot->handle);
#else
      pthread_join(slot->pthread, NULL);
#endif
    }
  }
  BLI_freelistN(threadbase);
}

int BLI_system_thread_count(void)
{
#ifdef _WIN32
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return (int)info.dwNumberOfProcessors;
#else
  const long n = sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? (int)n : 1;
#endif
}

#include "BLI_mempool.h"
#include "BLI_task.h"

/* -- parallel range (Blender's BLI_task_parallel_range) ----------------------------
 * One job at a time on a persistent pool: workers sleep on `g_work`, take iterations
 * under the mutex, and the last one out wakes the caller on `g_done`. */

#define RANGE_THREADS_MAX 8

typedef struct RangeJob {
  TaskParallelRangeFunc func;
  void *userdata;
  int next, stop;
  int want;     /* workers that may still join this job */
  int busy;     /* workers inside this job */
  unsigned gen; /* moves for every job */
  bool running;
} RangeJob;

static ThreadMutex g_range_mutex;
static ThreadCondition g_range_work, g_range_done;
static RangeJob g_range;
static int g_range_workers = -1;

/* Run iterations until none is left. The mutex is held on entry and on exit. */
static void range_drain(const TaskParallelTLS *tls)
{
  while (g_range.next < g_range.stop) {
    const int i = g_range.next++;
    BLI_mutex_unlock(&g_range_mutex);
    g_range.func(g_range.userdata, i, tls);
    BLI_mutex_lock(&g_range_mutex);
  }
}

static void *range_worker(void *arg)
{
  (void)arg;
  const TaskParallelTLS tls = {NULL};
  unsigned seen = 0;
  BLI_mutex_lock(&g_range_mutex);
  for (;;) {
    while (g_range.gen == seen) {
      BLI_condition_wait(&g_range_work, &g_range_mutex);
    }
    seen = g_range.gen;
    if (g_range.want <= 0) {
      continue;
    }
    g_range.want--;
    g_range.busy++;
    range_drain(&tls);
    if (--g_range.busy == 0) {
      BLI_condition_notify_all(&g_range_done);
    }
  }
  return NULL;
}

#ifdef _WIN32
static DWORD WINAPI range_worker_win(LPVOID arg)
{
  range_worker(arg);
  return 0;
}
#endif

/* Create the sync objects and start the workers. It runs once, see range_pool. */
static void range_init(void)
{
  BLI_mutex_init(&g_range_mutex);
  BLI_condition_init(&g_range_work);
  BLI_condition_init(&g_range_done);
  int n = BLI_system_thread_count();
  n = n < 1 ? 1 : (n > RANGE_THREADS_MAX ? RANGE_THREADS_MAX : n);
  int started = 0;
  for (int t = 1; t < n; t++) {
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, range_worker_win, NULL, 0, NULL);
    if (!h) {
      break;
    }
    CloseHandle(h);
#else
    pthread_t th;
    if (pthread_create(&th, NULL, range_worker, NULL) != 0) {
      break;
    }
    pthread_detach(th);
#endif
    started++;
  }
  g_range_workers = started;
}

/* Start the workers once. Other callers wait here until the first one is done. */
#ifdef _WIN32
static INIT_ONCE g_range_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK range_init_win(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
  (void)once;
  (void)param;
  (void)ctx;
  range_init();
  return TRUE;
}

static int range_pool(void)
{
  InitOnceExecuteOnce(&g_range_once, range_init_win, NULL, NULL);
  return g_range_workers;
}
#else
static pthread_once_t g_range_once = PTHREAD_ONCE_INIT;

static int range_pool(void)
{
  pthread_once(&g_range_once, range_init);
  return g_range_workers;
}
#endif

int BLI_task_scheduler_num_threads(void)
{
  return range_pool() + 1;
}

void BLI_task_parallel_range(const int start,
                             const int stop,
                             void *userdata,
                             TaskParallelRangeFunc func,
                             const TaskParallelSettings *settings)
{
  const TaskParallelTLS tls = {NULL};
  const int count = stop - start;
  if (count <= 0) {
    return;
  }
  const int per = settings->min_iter_per_thread > 0 ? settings->min_iter_per_thread : 1;
  int threads = settings->use_threading ? BLI_task_scheduler_num_threads() : 1;
  if (count / per < threads) {
    threads = count / per > 0 ? count / per : 1;
  }
  if (threads > 1) {
    BLI_mutex_lock(&g_range_mutex);
    if (g_range.running) { /* nested or from another thread: serial */
      BLI_mutex_unlock(&g_range_mutex);
      threads = 1;
    }
  }
  if (threads <= 1) {
    for (int i = start; i < stop; i++) {
      func(userdata, i, &tls);
    }
    return;
  }
  g_range.func = func;
  g_range.userdata = userdata;
  g_range.next = start;
  g_range.stop = stop;
  g_range.want = threads - 1;
  g_range.busy = 0;
  g_range.running = true;
  g_range.gen++;
  BLI_condition_notify_all(&g_range_work);
  range_drain(&tls);
  g_range.want = 0; /* workers that wake late do not join */
  while (g_range.busy > 0) {
    BLI_condition_wait(&g_range_done, &g_range_mutex);
  }
  g_range.running = false;
  BLI_mutex_unlock(&g_range_mutex);
}

void BLI_task_parallel_mempool(struct BLI_mempool *UNUSED(pool),
                               void *UNUSED(userdata),
                               TaskParallelMempoolFunc UNUSED(func),
                               const bool UNUSED(use_threading))
{
  /* no-op */
}
