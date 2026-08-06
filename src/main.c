#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>

#ifdef _MSC_VER
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include "lvgl/lvgl.h"
#include <SDL.h>
#include "hal/hal.h"

#include "echoear_pro_ui.h"
#include "echoear_app_state.h"
#include "echoear_mock_api.h"
#include "echoear_provisioning.h"
#include "echoear_provisioning_mock.h"
#include "echoear_first_boot.h"
#include "echoear_first_boot_mock.h"
#include "echoear_softap.h"
#include "echoear_softap_mock.h"
#include "echoear_captive_portal.h"
#include "echoear_captive_portal_mock.h"
#include "echoear_wifi_manager.h"
#include "echoear_wifi_manager_mock.h"

#if LV_USE_OS != LV_OS_FREERTOS

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    lv_init();

    /* Circular preview 360x360 */
    sdl_hal_init(360, 360);

    echoear_app_state_init();
    echoear_provisioning_init();
    echoear_softap_init();
    echoear_captive_portal_init();
    echoear_wifi_manager_init();

    echoear_first_boot_init();
    echoear_first_boot_mock_load(
        "mock/device_storage.txt");

    echoear_pro_ui_create();

    echoear_pro_ui_set_state(ECHOEAR_FACE_NORMAL_IDLE);
    echoear_mock_api_load("mock/state.txt");
    echoear_provisioning_mock_load(
        "mock/provisioning.txt");

    echoear_softap_mock_load(
        "mock/softap.txt");

    echoear_captive_portal_mock_load(
        "mock/captive_portal.txt");

    echoear_wifi_manager_mock_load(
        "mock/wifi_manager.txt");

    uint32_t api_tick = 0;

    // echoear_app_state_init();
    // echoear_mock_api_load("mock/state.txt");
    // echoear_pro_ui_create();

    // echoear_face_state_t preview_face = ECHOEAR_FACE_CAR_OBD_READY; /*อยากดูอะไรเปลี่ยนตรงนี้*/
    // echoear_pro_ui_set_state(preview_face);

    // uint32_t api_tick = 0;

    while (1)
    {
        uint32_t sleep_time_ms = lv_timer_handler();

        if (sleep_time_ms == LV_NO_TIMER_READY)
        {
            sleep_time_ms = LV_DEF_REFR_PERIOD;
        }

        api_tick += sleep_time_ms;

        if (api_tick > 1000)
        {
            api_tick = 0;
            echoear_mock_api_load("mock/state.txt");
    echoear_provisioning_mock_load(
        "mock/provisioning.txt");

    echoear_softap_mock_load(
        "mock/softap.txt");

    echoear_captive_portal_mock_load(
        "mock/captive_portal.txt");

    echoear_wifi_manager_mock_load(
        "mock/wifi_manager.txt");
        }

#ifdef _MSC_VER
        Sleep(sleep_time_ms);
#else
        usleep(sleep_time_ms * 1000);
#endif
    }

    return 0;
}

#endif

/*ECHOEAR_FACE_NORMAL_IDLE,
ECHOEAR_FACE_NORMAL_ANGRY,
ECHOEAR_FACE_NORMAL_CONFUSED,
ECHOEAR_FACE_NORMAL_HAPPY,
ECHOEAR_FACE_NORMAL_LISTENING,
ECHOEAR_FACE_NORMAL_SAD,
ECHOEAR_FACE_NORMAL_SLEEPING,
ECHOEAR_FACE_NORMAL_SPEAKING,
ECHOEAR_FACE_NORMAL_SURPRISED,
ECHOEAR_FACE_NORMAL_THINKING,
ECHOEAR_FACE_NORMAL_WINK,

ECHOEAR_FACE_SYSTEM_WIFI_SETUP,
ECHOEAR_FACE_SYSTEM_OTA_UPDATING,
ECHOEAR_FACE_SYSTEM_LOW_BATTERY,
ECHOEAR_FACE_SYSTEM_ERROR,

ECHOEAR_FACE_CAR_OBD_CONNECTING,
ECHOEAR_FACE_CAR_OBD_READY,
ECHOEAR_FACE_CAR_OBD_ERROR*/