#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/split/central.h>

#include <raw_hid/events.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * Status report, CONFIG_RAW_HID_REPORT_SIZE bytes, unused bytes zero:
 *   [0]     report id
 *   [1]     protocol version
 *   [2]     highest active layer index
 *   [3]     explicit modifiers (HID bitmask)
 *   [4..5]  peripheral battery %, 0 = unknown
 *   [8..31] layer display name, NUL padded
 */
#define REPORT_ID 0xC5
#define PROTOCOL_VERSION 1
#define OFF_LAYER 2
#define OFF_MODS 3
#define OFF_BATTERY 4
#define OFF_NAME 8
#define REPORT_LEN 32

BUILD_ASSERT(CONFIG_RAW_HID_REPORT_SIZE >= REPORT_LEN, "raw HID report too small");

static uint8_t last_report[REPORT_LEN];

static void build_report(uint8_t *report) {
    memset(report, 0, REPORT_LEN);
    report[0] = REPORT_ID;
    report[1] = PROTOCOL_VERSION;

    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    report[OFF_LAYER] = index;
    report[OFF_MODS] = zmk_hid_get_explicit_mods();

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    for (uint8_t i = 0; i < MIN(ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT, 2); i++) {
        zmk_split_central_get_peripheral_battery_level(i, &report[OFF_BATTERY + i]);
    }
#endif

    const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));
    if (name) {
        strncpy((char *)&report[OFF_NAME], name, REPORT_LEN - OFF_NAME - 1);
    }
}

static void send_status(struct k_work *work) {
    uint8_t report[REPORT_LEN];

    build_report(report);
    if (memcmp(report, last_report, REPORT_LEN) == 0) {
        return;
    }
    memcpy(last_report, report, REPORT_LEN);

    raise_raw_hid_sent_event((struct raw_hid_sent_event){.data = report, .length = REPORT_LEN});
}

static K_WORK_DEFINE(send_work, send_status);

static int status_listener(const zmk_event_t *eh) {
    k_work_submit(&send_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cygnus_status, status_listener);
ZMK_SUBSCRIPTION(cygnus_status, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(cygnus_status, zmk_keycode_state_changed);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
ZMK_SUBSCRIPTION(cygnus_status, zmk_peripheral_battery_state_changed);
#endif
