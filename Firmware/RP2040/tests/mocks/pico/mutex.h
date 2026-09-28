// Host mock of the Pico SDK mutex API (single-threaded tests).
#pragma once

typedef struct {
    int locked;
} mutex_t;

static inline void mutex_init(mutex_t* m) { m->locked = 0; }
static inline void mutex_enter_blocking(mutex_t* m) { m->locked = 1; }
static inline void mutex_exit(mutex_t* m) { m->locked = 0; }
