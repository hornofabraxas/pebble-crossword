/*
 * phone.c: everything that crosses AppMessage.
 *
 * Voice only. The watch ships {CMD_NORMALIZE, SEQ, MODE, TRANSCRIPT, ANSWER,
 * LEN}; pkjs replies {RESP_LETTERS, RESP_STATUS, RESP_CONF, SEQ}. SEQ is a
 * per-request tag so a reply that arrives after the player moved on is dropped
 * instead of filling the wrong entry. Puzzles are bundled, so nothing else
 * crosses the wire.
 */
#include "crossword.h"

#define CMD_NORMALIZE 1

static bool send_begin(DictionaryIterator **it) {
  return app_message_outbox_begin(it) == APP_MSG_OK;
}

void phone_send_normalize(void) {
  DictionaryIterator *it;
  if (!send_begin(&it)) return;
  char answer[24];
  puzzle_answer(puzzle_cur(), answer, sizeof(answer));
  g->voice_seq++;
  g->voice_entry = g->cur_entry;
  dict_write_int32(it, MESSAGE_KEY_CMD, CMD_NORMALIZE);
  dict_write_int32(it, MESSAGE_KEY_SEQ, g->voice_seq);
  dict_write_int32(it, MESSAGE_KEY_MODE, g->set.check);
  dict_write_cstring(it, MESSAGE_KEY_TRANSCRIPT, g->transcript);
  dict_write_cstring(it, MESSAGE_KEY_ANSWER, answer);
  dict_write_int32(it, MESSAGE_KEY_LEN, puzzle_cur()->len);
  app_message_outbox_send();
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  Tuple *letters = dict_find(it, MESSAGE_KEY_RESP_LETTERS);
  Tuple *status = dict_find(it, MESSAGE_KEY_RESP_STATUS);
  Tuple *conf = dict_find(it, MESSAGE_KEY_RESP_CONF);
  Tuple *seq = dict_find(it, MESSAGE_KEY_SEQ);
  if (!letters && !status) return;
  play_voice_result(letters ? letters->value->cstring : "",
                    status ? status->value->int32 : 1,
                    conf ? conf->value->int32 : 0,
                    seq ? seq->value->int32 : g->voice_seq);
}

void phone_init(void) {
  app_message_register_inbox_received(inbox_received);
  // Inbox holds a voice reply (up to 24 letters, three ints and their tuple
  // headers: under 100 bytes); outbox the normalize request (a ~200-char
  // transcript plus the answer and four ints). Both buffers come out of the
  // app heap, so they are sized to the traffic, not the platform maximum.
  app_message_open(256, 512);
}
