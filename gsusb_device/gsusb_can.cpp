#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/twai.h"
#include "esp_log.h"

#include "board_pins.h"
#include "dbg_helpers.h"
#include "gs_usb.h"
#include "gsusb_can.h"

#include "led_service.h"

static volatile bool can_active = false;
static volatile bool can_initialized = false;
static SemaphoreHandle_t can_mutex = nullptr;

void gsusb_can_init(void)
{
    if (!can_mutex)
    {
        can_mutex = xSemaphoreCreateMutex();
        if (!can_mutex)
        {
            GSUSB_LOGE("GSUSB", "Failed to create CAN mutex");
        }
        else
        {
            GSUSB_LOGI("GSUSB", "CAN mutex created");
        }
    }
    can_active = false;
    can_initialized = false;
}

SemaphoreHandle_t gsusb_can_get_mutex(void)
{
    return can_mutex;
}

bool gsusb_can_is_initialized(void)
{
    return can_initialized;
}

bool gsusb_can_is_active(void)
{
    return can_active;
}

bool gsusb_can_set_bittiming(const struct gs_device_bittiming *bt)
{
    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(TX_CAN, RX_CAN, TWAI_MODE_NORMAL);
    g_config.tx_queue_len = 20;
    g_config.rx_queue_len = 20;
    g_config.alerts_enabled = TWAI_ALERT_RX_DATA |
                              TWAI_ALERT_BUS_OFF |
                              TWAI_ALERT_BUS_ERROR;

    twai_timing_config_t t_config = {};
    t_config.brp = bt->brp;
    t_config.tseg_1 = bt->prop_seg + bt->phase_seg1;
    t_config.tseg_2 = bt->phase_seg2;
    t_config.sjw = bt->sjw;
    t_config.triple_sampling = false;

    GSUSB_LOGI("GSUSB",
               "set_can_bittiming: brp=%" PRIu32
               " prop=%" PRIu32 " phase1=%" PRIu32
               " phase2=%" PRIu32 " sjw=%" PRIu32
               " => tseg1=%u tseg2=%u",
               bt->brp, bt->prop_seg, bt->phase_seg1,
               bt->phase_seg2, bt->sjw,
               (unsigned)t_config.tseg_1,
               (unsigned)t_config.tseg_2);

    if (t_config.tseg_1 > 16 || t_config.tseg_2 > 8)
    {
        GSUSB_LOGE("GSUSB", "Bit timing out of range, rejecting");
        return false;
    }

    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (can_initialized)
    {
        GSUSB_LOGI("GSUSB", "Reconfig CAN: stopping + uninstall before reinstall");

        if (can_active)
        {
            esp_err_t stop_err = twai_stop();
            GSUSB_LOGI("GSUSB", "twai_stop (reconfig) returned: %s",
                       esp_err_to_name(stop_err));
            can_active = false;
        }

        esp_err_t un_err = twai_driver_uninstall();
        if (un_err != ESP_OK)
        {
            GSUSB_LOGE("GSUSB", "twai_driver_uninstall (reconfig) failed: %s",
                       esp_err_to_name(un_err));
        }
    }

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK)
    {
        GSUSB_LOGE("GSUSB", "twai_driver_install failed: %s",
                   esp_err_to_name(err));
        can_initialized = false;
        return false;
    }

    can_initialized = true;
    GSUSB_LOGI("GSUSB", "twai_driver_install OK");
    return true;
}

esp_err_t gsusb_can_start(void)
{
    if (!can_initialized)
    {
        GSUSB_LOGE("GSUSB", "gsusb_can_start: CAN not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (can_active)
    {
        GSUSB_LOGI("GSUSB", "gsusb_can_start: CAN already active");
        return ESP_OK;
    }

    esp_err_t err = twai_start();
    if (err == ESP_OK)
    {
        can_active = true;
        LedService::getInstance().setStatusLed(LED_ACTIVE);
        GSUSB_LOGI("GSUSB", "twai_start OK, CAN active");
    }
    else
    {
        LedService::getInstance().setStatusLed(LED_ERROR);
        GSUSB_LOGE("GSUSB", "twai_start failed: %s", esp_err_to_name(err));
    }
    return err;
}

void gsusb_can_stop(void)
{
    if (!can_initialized)
    {
        GSUSB_LOGW("GSUSB", "gsusb_can_stop called but CAN not initialized");
        return;
    }
    if (can_active)
    {
        esp_err_t err = twai_stop();
        if (err == ESP_OK)
        {
            LedService::getInstance().setStatusLed(LED_OFF);
            GSUSB_LOGI("GSUSB", "CAN stopped");
        }
        else
        {
            LedService::getInstance().setStatusLed(LED_ERROR);
            GSUSB_LOGE("GSUSB", "twai_stop failed: %s", esp_err_to_name(err));
        }
        can_active = false;
    }
    else
    {
        GSUSB_LOGI("GSUSB", "gsusb_can_stop: CAN already inactive");
    }
}

esp_err_t gsusb_can_receive(twai_message_t *msg, TickType_t timeout)
{
    if (!can_initialized || !can_active)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return twai_receive(msg, timeout);
}

esp_err_t gsusb_can_transmit(const twai_message_t *msg, TickType_t timeout)
{
    if (!can_initialized || !can_active)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return twai_transmit(msg, timeout);
}
