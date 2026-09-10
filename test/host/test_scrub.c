/*
 * Host tests for the letter scrubber (src/c/scrub.c): synthetic finger traces
 * in, letter steps out. The bounds describe the tuned behaviour as it is, so a
 * change to a knob that alters the feel on the wrist shows up here first.
 * Build and run: bash test/host/run.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include "scrub.h"

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t now;
static Scrub S;

static void start(int x) { now = 1000; scrub_reset(&S, x, now); }
// A straight drag: n samples, each dx pixels after dt ms. Returns the steps.
static int drag(int *x, int dx, int dt, int n, int mode, int speed) {
  int steps = 0;
  for (int i = 0; i < n; i++) { *x += dx; now += dt; steps += scrub_feed(&S, *x, now, mode, speed); }
  return steps;
}

static void test_fixed_pace(void) {
  int x = 100;
  start(x);
  int steps = drag(&x, 3, 20, 80, SCRUB_MODE_FIXED, SCRUB_SPEED_DEFAULT);   // 240 px at 150 px/s
  CHECK(steps >= 18 && steps <= 20, "fixed: 240 px should give about 20 letters, got %d", steps);
  start(x);
  int back = drag(&x, -3, 20, 80, SCRUB_MODE_FIXED, SCRUB_SPEED_DEFAULT);
  CHECK(back <= -18 && back >= -20, "fixed: dragging back should give the same count negative, got %d", back);
}

static void test_adaptive_speed_ramp(void) {
  int x = 20;
  start(x);
  int slow = drag(&x, 2, 20, 110, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);   // 220 px at 100 px/s: the plateau
  x = 20; start(x);
  int fast = drag(&x, 14, 20, 16, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);   // 224 px at 700 px/s
  CHECK(slow >= 8 && slow <= 10, "adaptive plateau: 220 px at 100 px/s should give about 10 letters, got %d", slow);
  CHECK(fast >= 2 * slow, "adaptive: a flying finger should get at least twice the letters per px (slow %d, fast %d)", slow, fast);
}

static void test_speed_setting(void) {
  int x = 20;
  start(x);
  int def = drag(&x, 2, 20, 110, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);
  x = 20; start(x);
  int slow = drag(&x, 2, 20, 110, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_SLOW);
  x = 20; start(x);
  int fast = drag(&x, 2, 20, 110, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_FAST);
  CHECK(slow < def && def < fast, "Scroll speed: Slow < Default < Fast letters for the same drag (got %d, %d, %d)", slow, def, fast);
}

// The settle lock: a rest on a letter, then the sideways twitch of a lifting
// finger, must not move the letter. Moving on afterwards must still scroll.
static void test_fixed_speed_setting(void) {
  int x = 100;
  start(x);
  int def = drag(&x, 3, 20, 80, SCRUB_MODE_FIXED, SCRUB_SPEED_DEFAULT);
  x = 100; start(x);
  int slow = drag(&x, 3, 20, 80, SCRUB_MODE_FIXED, SCRUB_SPEED_SLOW);
  x = 100; start(x);
  int fast = drag(&x, 3, 20, 80, SCRUB_MODE_FIXED, SCRUB_SPEED_FAST);
  CHECK(slow >= 13 && slow <= 15, "fixed slow: 240 px at 16.8 px per letter is about 14 (got %d)", slow);
  CHECK(fast >= 27 && fast <= 30, "fixed fast: 240 px at 8.4 px per letter is about 28 (got %d)", fast);
  CHECK(slow < def && def < fast, "fixed: Slow < Default < Fast (got %d, %d, %d)", slow, def, fast);
}

static void test_settle_lock(void) {
  int x = 20;
  start(x);
  drag(&x, 2, 20, 22, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);   // 44 px in: on a letter
  int during = drag(&x, 0, 20, 20, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);   // 400 ms still
  during += drag(&x, 1, 20, 1, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);       // 1 px sensor jitter ...
  during += drag(&x, -1, 20, 1, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);      // ... and back
  during += drag(&x, 3, 20, 1, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);       // the lift-off twitch (under SCRUB_UNLOCK)
  during += drag(&x, -3, 20, 1, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);
  CHECK(during == 0, "settle lock: a rest, jitter and a 3 px twitch must not step (got %d)", during);
  int after = drag(&x, 2, 20, 30, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);    // 60 px on: the lock must let go
  CHECK(after >= 1, "settle lock: moving on after a rest must scroll again (got %d)", after);
}

// A creep at 5 px/s rests between every sample by the lock's measure, so the
// lock engages over and over; it must still hand out letters.
static void test_creep_never_stalls(void) {
  int x = 20;
  start(x);
  int steps = drag(&x, 1, 200, 150, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);   // 150 px over 30 s
  CHECK(steps >= 2, "creep: 150 px at 5 px/s must not stall (got %d)", steps);
  CHECK(steps <= 150 / SCRUB_PX_SLOW, "creep: creeping is dearer than the plateau (got %d)", steps);
}

// A small wobble against the grain keeps the progress banked; a real reversal
// drops it.
static void test_reversal_guard(void) {
  int x = 20;
  start(x);
  int steps = drag(&x, 2, 20, 9, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);   // 18 px: short of a letter
  steps += drag(&x, -2, 20, 1, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);     // a 2 px wobble
  steps += drag(&x, 2, 20, 4, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);      // and on
  CHECK(steps == 1, "wobble: 18 px, a 2 px wobble, then 8 px more should land one letter (got %d)", steps);
  x = 20; start(x);
  steps = drag(&x, 2, 20, 9, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);
  steps += drag(&x, -6, 20, 2, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);     // a real reversal, 12 px back
  steps += drag(&x, 2, 20, 4, SCRUB_MODE_ADAPTIVE, SCRUB_SPEED_DEFAULT);
  CHECK(steps == 0, "reversal: 18 px, 12 px back, then 8 px on should not land a letter (got %d)", steps);
}

// The rail chevron follows the filtered movement, not the raw sample.
static void test_direction(void) {
  int x = 50;
  start(x);
  drag(&x, 3, 20, 3, SCRUB_MODE_FIXED, SCRUB_SPEED_DEFAULT);
  CHECK(S.dir == 1, "dir: a drag right reports +1 (got %d)", S.dir);
  drag(&x, -3, 20, 3, SCRUB_MODE_FIXED, SCRUB_SPEED_DEFAULT);
  CHECK(S.dir == -1, "dir: a drag left reports -1 (got %d)", S.dir);
  drag(&x, 0, 20, 1, SCRUB_MODE_FIXED, SCRUB_SPEED_DEFAULT);
  CHECK(S.dir == -1, "dir: a still sample right after a drag still reports the filter's tail (got %d)", S.dir);
  drag(&x, 0, 20, 6, SCRUB_MODE_FIXED, SCRUB_SPEED_DEFAULT);
  CHECK(S.dir == 0, "dir: once the filter has settled a still sample reports 0 (got %d)", S.dir);
}

int main(void) {
  test_fixed_pace();
  test_adaptive_speed_ramp();
  test_speed_setting();
  test_fixed_speed_setting();
  test_settle_lock();
  test_creep_never_stalls();
  test_reversal_guard();
  test_direction();
  if (fails) { printf("test_scrub: %d failed\n", fails); return 1; }
  printf("test_scrub: OK\n");
  return 0;
}
