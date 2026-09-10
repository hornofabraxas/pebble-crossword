/*
 * scrub.c: how finger pixels become letter steps (see scrub.h).
 *
 * Finger right = later letters. The distance per letter follows the finger's
 * speed (the Digital Crown idea): anything up to SCRUB_V_SLOW is the precise
 * plateau at SCRUB_PX_SLOW pixels per letter, a flying finger gets
 * SCRUB_PX_FAST, in between a straight line. Speeding up is smoothed so a flick
 * does not jump; slowing down takes effect on the sample it happens, so the
 * letters get dear again the moment the finger settles on its target (the
 * symmetric average of v0.10 to v0.14 kept handing out cheap letters for a few
 * samples after the finger slowed, an overshoot of about one letter). Progress
 * carries across samples so a slow drag never stalls between letters; the raw
 * position is low-passed and only a reversal past SCRUB_REV drops progress, so a
 * wobble near the target does not flip the letter.
 *
 * Two more for the final letter. Creep: below SCRUB_V_CREEP the letters get
 * dearer still, up to SCRUB_PX_CREEP for a finger that is barely moving, so the
 * last approach is the most precise part of the scroll. Settle lock: a rest of
 * SCRUB_STILL_MS on a letter (raw x within 1 px, so sensor jitter still counts
 * as rest) locks it, and nothing scrolls until the finger has moved SCRUB_UNLOCK
 * from the rest point. That swallows the sideways wobble a lifting finger tends
 * to make, and the drift of one resting, on exactly the letter just chosen; a
 * finger that is still moving never rests long enough to notice. Progress banked
 * before the rest is kept, and once the finger has moved on, all of that
 * movement counts (measured from the rest point), so a slow creep is delayed a
 * few px after each pause but never stalls.
 */
#include "scrub.h"

void scrub_reset(Scrub *s, int x, uint32_t t_ms) {
  s->last = x;
  s->fx = x << 3;
  s->acc = 0;
  s->v = 0;
  s->t = t_ms;
  s->lock = false;
  s->rest_x = x;
  s->move_t = t_ms;
  s->dir = 0;
}

int scrub_feed(Scrub *s, int x, uint32_t t, int mode, int speed) {
  s->dir = 0;
  int adr = x - s->rest_x; if (adr < 0) adr = -adr;
  if (adr > 1) {   // raw movement past jitter
    if (!s->lock && t - s->move_t >= SCRUB_STILL_MS) {   // the finger rested on a letter: lock it
      s->lock = true;
      s->fx = s->rest_x << 3; s->last = s->rest_x;   // the filter waits at the rest point
      s->v = 0; s->t = t;
    } else if (!s->lock) s->rest_x = x;   // keep the rest point at the finger while it moves
    s->move_t = t;
  }
  if (s->lock) {
    if (adr < SCRUB_UNLOCK) return 0;   // a twitch: ignored, and gone for good if the finger lifts
    s->lock = false; s->rest_x = x;     // moved on: fall through, and it counts from the rest point
  }
  // Low-pass the raw touch x (alpha 1/2) so single-pixel sensor jitter does not
  // jerk the stepping or pollute the speed estimate; the delta is taken from the
  // filtered position.
  s->fx += ((x << 3) - s->fx) >> 1;
  int fx = s->fx >> 3;
  int d = fx - s->last;
  s->last = fx;
  int dt = (int)(t - s->t);
  s->t = t;
  if (dt < 1) dt = 1;
  if (dt > 200) dt = 200;
  int inst = (d < 0 ? -d : d) * 1000 / dt;         // px/s this sample
  if (inst < s->v) s->v = inst;                    // slowing down counts at once
  else s->v = (s->v * 2 + inst) / 3;               // speeding up is smoothed
  if (d == 0) return 0;
  int ppl = SCRUB_PX_FIXED;
  if (mode != SCRUB_MODE_FIXED) {
    if (s->v < SCRUB_V_CREEP) {
      ppl = SCRUB_PX_CREEP - (SCRUB_PX_CREEP - SCRUB_PX_SLOW) * s->v / SCRUB_V_CREEP;
    } else {
      int v = s->v < SCRUB_V_SLOW ? 0 : s->v - SCRUB_V_SLOW;
      ppl = SCRUB_PX_SLOW - (SCRUB_PX_SLOW - SCRUB_PX_FAST) * v / (SCRUB_V_FAST - SCRUB_V_SLOW);
      if (ppl < SCRUB_PX_FAST) ppl = SCRUB_PX_FAST;
    }
  }
  ppl = ppl * (speed == SCRUB_SPEED_SLOW ? 140 : speed == SCRUB_SPEED_FAST ? 70 : 100) / 100;   // Settings: Scroll speed
  if (ppl < 3) ppl = 3;
  // Only a real reversal (past SCRUB_REV) drops accumulated progress; a small
  // wobble near the target no longer flips the letter.
  if ((s->acc > 0 && d <= -SCRUB_REV) || (s->acc < 0 && d >= SCRUB_REV)) s->acc = 0;
  s->acc += d;
  s->dir = d > 0 ? +1 : -1;
  int steps = 0;
  while (s->acc >= ppl)  { steps++; s->acc -= ppl; }
  while (s->acc <= -ppl) { steps--; s->acc += ppl; }
  return steps;
}
