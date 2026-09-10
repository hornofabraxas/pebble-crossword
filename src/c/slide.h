/*
 * slide.h: a reusable "page turn" slide, shared by the play view and the Controls
 * cards. The caller keeps a content layer whose update proc draws the current
 * state; on a change of state the new content is rendered once, captured into an
 * offscreen bitmap, and slid in with a cheap per-frame blit (rather than
 * re-rendering the whole content every frame, which dropped frames). On memory
 * pressure the content layer itself is animated instead. See slide.c.
 */
#pragma once
#include <pebble.h>

// Default page-turn length. ~30fps animation service, so ~5 frames; short enough
// to feel quick, long enough not to read as a jump. Shared by every SlideView.
#define SLIDE_DEFAULT_MS 150

typedef struct {
  Layer *content;              // the caller's content layer (already in a window's tree)
  Layer *snap;                 // owned: sits directly above content, carries the snapshot
  GBitmap *bmp;                // owned: the captured content, reused across slides
  PropertyAnimation *anim;     // owned: the running animation, if any
  GRect full;                  // content's resting frame
  uint32_t duration;           // slide length, ms
  int phase;                   // 0 idle, 1 capture on the next paint, 2 blitting the snapshot
  int dx, dy;                  // where the snapshot starts, relative to rest
  bool active;                 // a slide is in progress (for input gating)
} SlideView;

// Set up a slide over `content` (which must already be added to its window's tree,
// with its resting frame set). Inserts an internal snapshot layer directly above
// `content`, so anything added above `content` later stays on top of the slide.
void slide_init(SlideView *sv, Layer *content, uint32_t duration_ms);
void slide_deinit(SlideView *sv);

// Start a slide. Update your state first; the new content enters from an offset of
// (dx, dy) and eases to rest. slide_go marks the content layer dirty for you.
void slide_go(SlideView *sv, int dx, int dy);
void slide_cancel(SlideView *sv);

bool slide_active(const SlideView *sv);     // a slide is running (ignore input)
bool slide_blanking(const SlideView *sv);   // the content update proc should draw only the blank ground
