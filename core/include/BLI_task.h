/* BUI-owned minimal task-parallelism surface.
 * The extracted core only references the parallel-mempool iterator plumbing;
 * real scheduling is out of scope (single-threaded stubs for now).
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __BUI_TASK_H__
#define __BUI_TASK_H__

#include "BLI_compiler_compat.h"
#include "BLI_sys_types.h"
#include "BLI_threads.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TaskParallelTLS {
  void *userdata_chunk;
} TaskParallelTLS;

typedef void (*TaskParallelMempoolFunc)(void *tls, void *elem);

/* Blender's BLI_task_parallel_range: func(userdata, iter, tls) for every iter in
 * [start, stop), on a persistent worker pool plus the calling thread. Iterations are
 * taken one at a time, so give it coarse work items. */
typedef void (*TaskParallelRangeFunc)(void *__restrict userdata,
                                      int iter,
                                      const TaskParallelTLS *__restrict tls);

typedef struct TaskParallelSettings {
  bool use_threading;
  int min_iter_per_thread;
} TaskParallelSettings;

BLI_INLINE void BLI_parallel_range_settings_defaults(TaskParallelSettings *settings)
{
  settings->use_threading = true;
  settings->min_iter_per_thread = 1;
}

void BLI_task_parallel_range(int start,
                             int stop,
                             void *userdata,
                             TaskParallelRangeFunc func,
                             const TaskParallelSettings *settings);

/* Threads a parallel range can use, the caller included (at most 8). */
int BLI_task_scheduler_num_threads(void);

/* Stub: runs nothing. Provide a scheduler-backed implementation later. */
struct BLI_mempool;
void BLI_task_parallel_mempool(struct BLI_mempool *pool,
                              void *userdata,
                              TaskParallelMempoolFunc func,
                              const bool use_threading);

#ifdef __cplusplus
}
#endif

#endif /* __BUI_TASK_H__ */
