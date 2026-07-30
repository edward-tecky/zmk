/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/ztest.h>

#include "gatt_rpc_transport.h"

static uint8_t ring_data[8];
static struct ring_buf ring;
static const uint8_t payload[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
static uint32_t notify_count;
static uint32_t ring_size_at_notify;
static K_SEM_DEFINE(space_check_entered, 0, 2);
static K_SEM_DEFINE(space_check_release, 0, 2);
static bool pause_space_check;
static atomic_t space_check_count;

uint32_t zmk_test_ring_buf_space_get(struct ring_buf *ring) {
    if (pause_space_check) {
        atomic_inc(&space_check_count);
        k_sem_give(&space_check_entered);
        k_sem_take(&space_check_release, K_FOREVER);
    }

    return ring_buf_space_get(ring);
}

struct ring_buf *zmk_rpc_get_rx_buf(void) { return &ring; }

void zmk_rpc_rx_notify(void) {
    ring_size_at_notify = ring_buf_size_get(&ring);
    notify_count++;
}

static void before(void *fixture) {
    ARG_UNUSED(fixture);

    ring_buf_init(&ring, sizeof(ring_data), ring_data);
    notify_count = 0;
    ring_size_at_notify = 0;
    pause_space_check = false;
    atomic_clear(&space_check_count);
    k_sem_reset(&space_check_entered);
    k_sem_reset(&space_check_release);
    zmk_studio_gatt_rpc_rx_set_active_for_test(true);
}

struct writer_context {
    ssize_t result;
};

static struct k_thread first_thread;
static struct k_thread second_thread;
K_THREAD_STACK_DEFINE(first_stack, 1024);
K_THREAD_STACK_DEFINE(second_stack, 1024);

static void writer_thread(void *context, void *unused1, void *unused2) {
    ARG_UNUSED(unused1);
    ARG_UNUSED(unused2);

    struct writer_context *writer = context;
    writer->result = zmk_studio_rpc_rx_write(&ring, payload, 5);
}

ZTEST(studio_gatt_rx, test_concurrent_writers_are_serialized_without_partial_enqueue) {
    struct writer_context first = {.result = INT_MIN};
    struct writer_context second = {.result = INT_MIN};

    pause_space_check = true;
    k_thread_create(&first_thread, first_stack, K_THREAD_STACK_SIZEOF(first_stack), writer_thread,
                    &first, NULL, NULL, 0, 0, K_NO_WAIT);
    zassert_equal(k_sem_take(&space_check_entered, K_SECONDS(1)), 0);

    k_thread_create(&second_thread, second_stack, K_THREAD_STACK_SIZEOF(second_stack), writer_thread,
                    &second, NULL, NULL, 0, 0, K_NO_WAIT);

    zassert_equal(k_sem_take(&space_check_entered, K_MSEC(50)), -EAGAIN);
    uint32_t concurrent_entries = atomic_get(&space_check_count);
    k_sem_give(&space_check_release);
    k_sem_give(&space_check_release);
    zassert_equal(k_thread_join(&first_thread, K_SECONDS(1)), 0);
    zassert_equal(k_thread_join(&second_thread, K_SECONDS(1)), 0);

    zassert_equal(concurrent_entries, 1);
    zassert_true((first.result == 5 && second.result == -ENOMEM) ||
                 (first.result == -ENOMEM && second.result == 5));
    zassert_equal(ring_buf_size_get(&ring), 5);
}

ZTEST(studio_gatt_rx, test_exact_free_space_is_accepted) {
    zassert_equal(zmk_studio_rpc_rx_write(&ring, payload, 8), 8);
    zassert_equal(ring_buf_size_get(&ring), 8);
}

ZTEST(studio_gatt_rx, test_free_plus_one_is_rejected_without_partial_enqueue) {
    zassert_equal(zmk_studio_rpc_rx_write(&ring, payload, 9), -ENOMEM);
    zassert_equal(ring_buf_size_get(&ring), 0);
}

ZTEST(studio_gatt_rx, test_full_ring_rejects_in_bounded_time) {
    ring_buf_put(&ring, payload, 8);
    zassert_equal(zmk_studio_rpc_rx_write(&ring, payload, 1), -ENOMEM);
    zassert_equal(ring_buf_size_get(&ring), 8);
}

ZTEST(studio_gatt_rx, test_gatt_write_rejects_nonzero_offset_without_enqueue_or_notify) {
    zassert_equal(zmk_studio_gatt_rpc_rx_write_for_test(payload, 1, 1),
                  BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET));
    zassert_equal(ring_buf_size_get(&ring), 0);
    zassert_equal(notify_count, 0);
}

ZTEST(studio_gatt_rx, test_gatt_write_maps_oversize_to_att_error_without_enqueue_or_notify) {
    zassert_equal(zmk_studio_gatt_rpc_rx_write_for_test(payload, 9, 0),
                  BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES));
    zassert_equal(ring_buf_size_get(&ring), 0);
    zassert_equal(notify_count, 0);
}

ZTEST(studio_gatt_rx, test_gatt_write_notifies_once_after_full_enqueue) {
    zassert_equal(zmk_studio_gatt_rpc_rx_write_for_test(payload, 8, 0), 8);
    zassert_equal(ring_buf_size_get(&ring), 8);
    zassert_equal(notify_count, 1);
    zassert_equal(ring_size_at_notify, 8);
}

ZTEST_SUITE(studio_gatt_rx, NULL, NULL, before, NULL, NULL);
