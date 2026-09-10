/*
 * scrub.h: the Type-mode letter scrubber, as a pure state machine.
 *
 * A finger dragging on the clue feeds raw (x, t) samples in; letter steps
 * come out. No Pebble dependencies, so the same code runs in the host tests
 * (test/host/test_scrub.c) against synthetic finger traces. Every tuning knob
 * lives here; play.c only applies the steps.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SCRUB_MODE_ADAPTIVE 0   // pixels per letter follow the finger's speed
#define SCRUB_MODE_FIXED    1   // a constant SCRUB_PX_FIXED per letter
#define SCRUB_SPEED_DEFAULT 0   // the tuned distances ...
#define SCRUB_SPEED_SLOW    1   // ... 1.4x the finger travel per letter
#define SCRUB_SPEED_FAST    2   // ... 0.7x

#define SCRUB_PX_SLOW 22     // adaptive: pixels per letter up to SCRUB_V_SLOW px/s (the precise plateau)
#define SCRUB_PX_FAST 6      // and when it flies (>= SCRUB_V_FAST px/s)
#define SCRUB_V_SLOW 150
#define SCRUB_V_FAST 700
#define SCRUB_PX_CREEP 30    // creeping onto the letter (a still finger): dearest of all, easing to SCRUB_PX_SLOW ...
#define SCRUB_V_CREEP 60     // ... by this speed
#define SCRUB_STILL_MS 300   // a rest this long on a letter locks it (settle lock) ...
#define SCRUB_UNLOCK 4       // ... until the finger has moved this far from the rest point
#define SCRUB_PX_FIXED 12    // fixed: every letter the same distance (9 in v0.8 to v0.14 ran away)
#define SCRUB_REV 3          // filtered px against the grain before progress is dropped (jitter guard)

typedef struct {
  int last;        // last filtered position used for stepping
  int fx;          // low-passed finger x, 1/8 px fixed point (jitter filter)
  int acc;         // pixels of drag not yet turned into a letter step
  int v;           // smoothed finger speed, px/s
  uint32_t t;      // time of the last sample, ms
  bool lock;       // the finger rested on a letter: no steps until it clearly moves on
  int rest_x;      // raw x the finger last moved from (the rest point while locked)
  uint32_t move_t; // time of the last raw movement past jitter, ms
  int dir;         // sign of the last filtered movement (0 when the sample moved nothing)
} Scrub;

void scrub_reset(Scrub *s, int x, uint32_t t_ms);
// Feed one sample. Returns the signed number of letter steps to apply (finger
// right = later letters = positive). mode is SCRUB_MODE_*, speed SCRUB_SPEED_*.
int scrub_feed(Scrub *s, int x, uint32_t t_ms, int mode, int speed);
