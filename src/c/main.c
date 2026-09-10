#include "crossword.h"

AppState *g;


static void init(void) {
  g = calloc(1, sizeof(AppState));
  if (!g) return;
  if (!puzzle_init_library()) g->puzzle_count = 0;   // nothing bundled; menu still works
  store_load();
  srand((unsigned)time(NULL));
  phone_init();
  // Third-party apps get no touch navigation until they opt in. With it, the
  // MenuLayer screens (home, picker, settings, all clues) scroll by drag and
  // select by tap. The play window owns touch itself (raw subscriber) while it
  // is on top, so the bridge is off there.
  app_touch_navigation_enable(true);
  menu_push_home();
  if (!store_onboard_seen()) explain_push(true);   // first install: walk the Controls cards, then drop to the menu
}

static void deinit(void) {
  if (!g) return;
  if (g->banner_timer) app_timer_cancel(g->banner_timer);
  play_deinit();
  menu_deinit();
  if (g->pbuf) free(g->pbuf);
  if (g->offsets) free(g->offsets);
  if (g->ids) free(g->ids);
  if (g->dict) free(g->dict);
  free(g);
}

int main(void) {
  init();
  if (g) app_event_loop();
  deinit();
}
