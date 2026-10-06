// Host mock of the Pico SDK critical section (single-threaded tests: just counts nesting).
#pragma once

struct critical_section_t {
    int depth = 0;
    bool initialized = false;
};

static inline void critical_section_init(critical_section_t* cs) { cs->initialized = true; }
static inline unsigned next_striped_spin_lock_num() { return 16; }
static inline void critical_section_init_with_lock_num(critical_section_t* cs, unsigned) { cs->initialized = true; }
static inline void critical_section_enter_blocking(critical_section_t* cs) { ++cs->depth; }
static inline void critical_section_exit(critical_section_t* cs) { --cs->depth; }
