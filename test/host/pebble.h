/*
 * pebble.h shim for the host tests: just enough of the SDK for crossword.h
 * and store.c to compile with the system compiler. Persist is provided by the
 * test itself (an in-memory table); nothing else from the SDK is called.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void *ResHandle;
typedef struct { uint8_t argb; } GColor;
typedef struct { int16_t x, y; } GPoint;
typedef struct { int16_t w, h; } GSize;
typedef struct { GPoint origin; GSize size; } GRect;
typedef struct AppTimer AppTimer;
typedef struct DictationSession DictationSession;

#define GColorBlack     ((GColor){ .argb = 0xC0 })
#define GColorLightGray ((GColor){ .argb = 0xEA })
#define GColorMelon     ((GColor){ .argb = 0xFB })

bool persist_exists(const uint32_t key);
int  persist_read_int(const uint32_t key);
int  persist_read_data(const uint32_t key, void *buffer, const size_t buffer_size);
int  persist_write_int(const uint32_t key, const int32_t value);
int  persist_write_data(const uint32_t key, const void *data, const size_t size);
int  persist_delete(const uint32_t key);
