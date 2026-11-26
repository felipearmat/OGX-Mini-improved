// Host stand-in for TaskQueue.h (the Pico timer and spin locks): the host drivers' delayed tasks
// do nothing in a host test.
#pragma once

#include <cstdint>
#include <functional>

class TaskQueue {
public:
    struct Core0 {
        static uint32_t get_new_task_id() { return 1; }
        static void cancel_delayed_task(uint32_t) {}
        static bool queue_delayed_task(uint32_t, uint32_t, bool, const std::function<void()>&) { return true; }
        static bool queue_task(const std::function<void()>&) { return true; }
    };
    struct Core1 : Core0 {};
};
