#define _POSIX_C_SOURCE 200809L
#include "lc_pcm_recorder.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

/* Build recorder with -Dclock_gettime=lc_test_clock_failure. */
int lc_test_clock_failure(clockid_t clock_id, struct timespec *time)
{
  (void)clock_id;
  (void)time;
  errno = EIO;
  return -1;
}
static void *open_device(void) { assert(!"must not open microphone"); return NULL; }
static int start_device(void *p) { (void)p; return 0; }
static int read_device(void *p, void *b, size_t n) { (void)p; (void)b; (void)n; return 0; }
static int abort_device(void *p) { (void)p; return 0; }
static void close_device(void *p) { (void)p; }
int main(void)
{
  const lc_pcm_recorder_ops_t ops = {.open = open_device, .start = start_device,
    .read = read_device, .abort = abort_device, .close = close_device};
  lc_pcm_recorder_t *recorder = lc_pcm_recorder_create(&ops, 40);
  assert(recorder != NULL);
  assert(lc_pcm_recorder_start(recorder) == LC_PCM_THREAD_ERROR);
  lc_pcm_recorder_destroy(recorder);
  puts("PASS: unavailable monotonic clock refuses capture before device open");
  return 0;
}
