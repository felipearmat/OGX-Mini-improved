// Joy-Con motion settings: defaults and sideways ("horizontal") rotation.
#include "Custom/JoyConSettings.h"
#include "test.h"

using joycon_settings::Orientation;

TEST(defaults_right_imu_vertical_pair_horizontal_solo) {
    const auto& s = joycon_settings::get();
    CHECK(s.pair_imu_right);
    CHECK(s.pair_orientation == Orientation::Vertical);
    CHECK(s.solo_orientation == Orientation::Horizontal);
}

TEST(vertical_leaves_motion_untouched) {
    int32_t a[3] = {1, 2, 3};
    int32_t g[3] = {4, 5, 6};
    joycon_settings::apply_orientation(true, Orientation::Vertical, a, g);
    CHECK_EQ(a[0], 1); CHECK_EQ(a[1], 2); CHECK_EQ(a[2], 3);
    CHECK_EQ(g[0], 4); CHECK_EQ(g[1], 5); CHECK_EQ(g[2], 6);
}

TEST(left_sideways_rotates_counter_clockwise) {
    int32_t a[3] = {10, 20, 30};
    int32_t g[3] = {-1, 7, 9};
    joycon_settings::apply_orientation(true, Orientation::Horizontal, a, g);
    CHECK_EQ(a[0], -20); CHECK_EQ(a[1], 10); CHECK_EQ(a[2], 30);
    CHECK_EQ(g[0], -7);  CHECK_EQ(g[1], -1); CHECK_EQ(g[2], 9);
}

TEST(right_sideways_rotates_clockwise) {
    int32_t a[3] = {10, 20, 30};
    int32_t g[3] = {-1, 7, 9};
    joycon_settings::apply_orientation(false, Orientation::Horizontal, a, g);
    CHECK_EQ(a[0], 20); CHECK_EQ(a[1], -10); CHECK_EQ(a[2], 30);
    CHECK_EQ(g[0], 7);  CHECK_EQ(g[1], 1);   CHECK_EQ(g[2], 9);
}

TEST(rotation_keeps_the_vector_length) {
    int32_t a[3] = {3, 4, 12};
    int32_t g[3] = {0, 0, 0};
    joycon_settings::apply_orientation(false, Orientation::Horizontal, a, g);
    CHECK_EQ(a[0] * a[0] + a[1] * a[1] + a[2] * a[2], 169);
}

TEST_MAIN()
