# Patched dependency trees (OGX-Mini-improved). Each tree (a git checkout: Bluepad32, its BTstack,
# the Pico SDK) gets its whole list of patches, in order, on a clean checkout of its commit. A stamp
# in the tree's git directory holds the hash of that list; while it matches, the tree is left as it
# is (configure stays quick and nothing is rebuilt). Any change (a patch added, edited, reordered
# or removed) resets the tree (git checkout + clean) and applies the list again, and a patch that
# does not apply fails the configure step.
#
# The old checks took "the last patch of a series reverse-applies" or "patch does not apply" as
# already applied: a tree patched with an older version of a patch kept it, so local builds (and
# the firmware flashed from them) differed from CI's, and a patch that no longer applied was
# skipped without a word. Change dependency code only through these patches: an edit made in the
# tree by hand is lost at the next reset and never reaches CI (Tools/check_patched_trees.py
# compares the trees with their patches).
function(ogxm_patch_tree NAME DIR)
    set(_patches ${ARGN})
    set(_list "")
    foreach (_patch IN LISTS _patches)
        file(SHA256 "${_patch}" _hash)
        get_filename_component(_file "${_patch}" NAME)
        string(APPEND _list "${_file} ${_hash}\n")
    endforeach ()
    string(SHA256 _stamp "${_list}")
    execute_process(
        COMMAND git rev-parse --absolute-git-dir
        WORKING_DIRECTORY ${DIR}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _git_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if (NOT _result EQUAL 0)
        message(FATAL_ERROR "${NAME}: ${DIR} is not a git checkout; the patches need one to start from")
    endif ()
    set(_stamp_file "${_git_dir}/ogxm-patches.sha256")
    if (EXISTS "${_stamp_file}")
        file(READ "${_stamp_file}" _stored)
        string(STRIP "${_stored}" _stored)
        if (_stored STREQUAL _stamp)
            message(STATUS "${NAME}: patches up to date")
            return()
        endif ()
    endif ()
    list(LENGTH _patches _count)
    message(STATUS "${NAME}: applying ${_count} patches to a clean checkout")
    file(REMOVE "${_stamp_file}")
    execute_process(COMMAND git checkout -- . WORKING_DIRECTORY ${DIR} RESULT_VARIABLE _result ERROR_VARIABLE _error)
    if (_result EQUAL 0)
        # What earlier patches added; ignored files and nested repositories stay.
        execute_process(COMMAND git clean -fdq WORKING_DIRECTORY ${DIR} RESULT_VARIABLE _result ERROR_VARIABLE _error)
    endif ()
    if (NOT _result EQUAL 0)
        message(FATAL_ERROR "${NAME}: could not reset ${DIR}: ${_error}")
    endif ()
    foreach (_patch IN LISTS _patches)
        execute_process(
            COMMAND git apply --ignore-whitespace ${_patch}
            WORKING_DIRECTORY ${DIR}
            RESULT_VARIABLE _result
            ERROR_VARIABLE _error
        )
        if (NOT _result EQUAL 0)
            message(FATAL_ERROR "${NAME}: ${_patch} does not apply: ${_error}")
        endif ()
    endforeach ()
    file(WRITE "${_stamp_file}" "${_stamp}\n")
endfunction()

# Bluepad32 (OGX-Mini-improved), in order. On felipearmat/bluepad32 `ogxm-mini-2026`:
# MegaCadeDev's fork (Switch 2, Steam Controller 2026, Xbox BLE) merged with upstream `develop`
# v5.0 (2026-10-29). Upstream has since taken our vibration enable (#226), rumble intensity
# (#227 / #232), request sleep (#229) and its own output ring buffer (b6531db): no patch for them.
# - Switch parser: setup robustness (per-step timeout + retries, stale-reply filtering, one pad
#   in setup at a time; its timers unlinked in the parser's deinit before the device is wiped);
#   pair IMU (a merged Joy-Con pair keeps and parses only the selected half's IMU); Joy-Con
#   pairing toggle; firmware version;
# - diagnostics hooks: raw input reports and BLE Device Information values;
# - DS4 parser: the latest touch points for touchpad passthrough; the DS4 Bluetooth report
#   interval set per platform (UNI_DS4_BT_POLL_INTERVAL_MS);
# - Switch parsers without the scan calls Bluepad32 itself marks deprecated (the ones they forward
#   to; no change in behaviour, no compiler warnings left).
# - full-scale triggers: 8-bit (DS3, DS4, DualSense, PS Move, Steam) and 5-bit (Wii Classic)
#   triggers widened to 10 bits by bit replication (255 -> 1023, as SDL's * 257), not * 4 / * 32
#   (255 -> 1020, 31 -> 992: a full trigger read 254 / 247 at 8 bits).
# - Switch parser: raw rumble, a Switch host's HD Rumble blocks sent to the pad unchanged (Switch
#   mode, Firmware/RP2040/docs/HD_Rumble.md); a pending strength rumble is called off through
#   Bluepad32's shared rumble timers.
# - BLE configuration service (v5.0, on by default): Kconfig leaves a bool option that is off
#   undefined, which uni_config.h took as on, so ESP-IDF builds could not turn it off. The adapter
#   keeps it off on both platforms (sdkconfig.h, ESP32 sdkconfig.defaults): it would replace our
#   ATT server (the web app over BLE) and open, with no password, remapping, disconnecting pads
#   and deleting pairings.
# Shared by the firmware builds and the host tests (Firmware/RP2040/tests), so all use one stamp.
function(ogxm_patch_bluepad32 EXTERNAL_DIR)
    set(_p "${EXTERNAL_DIR}/patches")
    ogxm_patch_tree("Bluepad32" "${EXTERNAL_DIR}/bluepad32"
        "${_p}/bluepad32_switch_setup_robustness.diff"
        "${_p}/bluepad32_switch_pair_imu.diff"
        "${_p}/bluepad32_switch_joycon_pairing_toggle.diff"
        "${_p}/bluepad32_switch_firmware_version.diff"
        "${_p}/bluepad32_diagnostics_hooks.diff"
        "${_p}/bluepad32_ds4_touchpad.diff"
        "${_p}/bluepad32_ds4_poll_interval.diff"
        "${_p}/bluepad32_no_deprecated_scan_calls.diff"
        "${_p}/bluepad32_full_scale_triggers.diff"
        "${_p}/bluepad32_switch_raw_rumble.diff"
        "${_p}/bluepad32_ble_service_kconfig_off.diff")
endfunction()

# Bluepad32's BTstack (bluekitchen, at the commit upstream Bluepad32 pins), in order:
# - Bluepad32's own L2CAP patch (external/patches in its tree: incoming connections with less
#   security than asked for, as its build applies it);
# - HOGP Report Map seed (MegaCadeDev/btstack 79c970b, which the fork pinned before): skip the
#   Report Map long read that hangs Xbox Series / SCUF BLE pads on CYW43 and use a known
#   descriptor; HID service discovery failures reported; more HID reports per service;
# - BTstack queued the HID Control Point command through the GATT query scheduler although it is
#   a write without response, which can leave Xbox BLE controllers connected but unable to finish
#   their setup (HID up, GETs return COMMAND_DISALLOWED, no input).
# - ESP32 port's btstack_config.h: sdkconfig.h first. It tested CONFIG_BTSTACK_INTERNAL_BT before
#   including it, so where it came first Controller-to-Host flow control (required for VHCI, by
#   its comment) was off and files disagreed on hci.h's structures.
function(ogxm_patch_btstack EXTERNAL_DIR)
    ogxm_patch_tree("BTstack" "${EXTERNAL_DIR}/bluepad32/external/btstack"
        "${EXTERNAL_DIR}/bluepad32/external/patches/0001-PATCH-l2cap-Allow-incoming-connection-with-not-enoug.patch"
        "${EXTERNAL_DIR}/patches/btstack_hogp_report_map_seed.diff"
        "${EXTERNAL_DIR}/patches/btstack_hids_control_point.diff"
        "${EXTERNAL_DIR}/patches/btstack_esp32_config_sdkconfig_first.diff")
endfunction()

# Pico SDK 2.3.1 (feature under test since 2026-10-27, was 2.1.0 with four patches): no patch. It
# has what 2.1.0 needed patching for: hids_host.c picked for Bluepad32's BTstack v1.8, the CYW43
# HCI pre-buffer (pico-sdk #2165), and the two timer bugs that left BTstack timers waiting for the
# next BT packet (pico-sdk #2838 and #2127 / #2190 / #2382). The tree is still reset to a clean
# checkout (an empty list), so a hand edit or a leftover 2.1.0 patch does not survive.
# CMakeLists.txt stops on an older SDK.
function(ogxm_patch_pico_sdk EXTERNAL_DIR)
    set(_sdk "${EXTERNAL_DIR}/pico-sdk")
    if (NOT EXISTS "${_sdk}/src/rp2_common/pico_btstack/CMakeLists.txt")
        return()  # another SDK (PICO_SDK_PATH elsewhere): left as it is
    endif ()
    ogxm_patch_tree("Pico SDK" "${_sdk}")
endfunction()

function(apply_lib_patches EXTERNAL_DIR)
    ogxm_patch_btstack(${EXTERNAL_DIR})
    ogxm_patch_bluepad32(${EXTERNAL_DIR})
    ogxm_patch_pico_sdk(${EXTERNAL_DIR})
endfunction()

