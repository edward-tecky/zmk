/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/physical_layouts.h>

int zmk_physical_layouts_get_selected_to_stock_position_map(uint32_t const **map) {
    static const uint32_t position_map[] = {0};

    *map = position_map;
    return ARRAY_SIZE(position_map);
}

int zmk_behavior_invoke_binding(const struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event, bool pressed) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    ARG_UNUSED(pressed);
    return 0;
}

ZTEST(position_bounds, test_position_at_keymap_length_is_rejected) {
    zassert_equal(zmk_keymap_position_state_changed(0, 1, true, k_uptime_get()), -EINVAL);
}

ZTEST(position_bounds, test_maximum_position_is_rejected) {
    zassert_equal(zmk_keymap_position_state_changed(0, UINT32_MAX, true, k_uptime_get()), -EINVAL);
}

ZTEST_SUITE(position_bounds, NULL, NULL, NULL, NULL, NULL);
