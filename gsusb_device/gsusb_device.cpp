#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/twai.h"
#include "tinyusb.h"
#include "class/vendor/vendor_device.h"
#include "esp_log.h"

#include "led_service.h"
#include "board_pins.h"
#include "gs_usb.h"
#include "dbg_helpers.h" 
#include "gsusb_device.h"


static volatile bool can_active       = false;  
static volatile bool can_initialized  = false;  

static SemaphoreHandle_t can_mutex    = nullptr;
static TaskHandle_t      h_usb_tx_task = nullptr;

#define CAN_CLOCK_SPEED 80000000UL

// USB descriptor
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN)


static uint32_t gs_resp_identify = 0xBEBAFECA;

static const struct gs_host_config gs_resp_host_format = {
    .byte_order = 0x0000beef
};

static struct gs_device_config gs_resp_conf = {
    0, // reserved1
    0, // reserved2
    0, // reserved3
    0, // icount (1 channel)
    2, // sw_version
    1  // hw_version
};

static struct gs_device_bt_const gs_resp_btc = {
    0,               // feature
    CAN_CLOCK_SPEED, // fclk_can
    1,               // tseg1_min
    16,              // tseg1_max
    1,               // tseg2_min
    8,               // tseg2_max
    4,               // sjw_max
    1,               // brp_min
    128,             // brp_max
    1                // brp_inc
};


static struct gs_device_bittiming temp_bt;
static struct gs_device_mode     temp_mode;
static uint32_t temp_host_format;

extern "C" void tinyusb_task(void *param);
extern "C" void can_rx_task(void *arg);
extern "C" void usb_tx_task(void *arg);


static bool set_can_bittiming(const struct gs_device_bittiming *bt)
{
    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(TX_CAN, RX_CAN, TWAI_MODE_NORMAL);
    g_config.tx_queue_len = 20;
    g_config.rx_queue_len = 20;
    g_config.alerts_enabled = TWAI_ALERT_RX_DATA |
                              TWAI_ALERT_BUS_OFF |
                              TWAI_ALERT_BUS_ERROR;

    twai_timing_config_t t_config = {};
    t_config.brp             = bt->brp;
    t_config.tseg_1          = bt->prop_seg + bt->phase_seg1;  
    t_config.tseg_2          = bt->phase_seg2;
    t_config.sjw             = bt->sjw;
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

    // Reinstall driver if it was already initialized
    if (can_initialized)
    {
        GSUSB_LOGI("GSUSB", "Reconfig CAN: stopping + uninstall before reinstall");
        if (can_active)
        {
            esp_err_t stop_err = twai_stop();
            GSUSB_LOGI("GSUSB", "twai_stop (reconfig) returned: %s", esp_err_to_name(stop_err));
            can_active = false;
        }

        esp_err_t un_err = twai_driver_uninstall();
        if (un_err != ESP_OK)
        {
            GSUSB_LOGE("GSUSB", "twai_driver_uninstall (reconfig) failed: %s", esp_err_to_name(un_err));
           
        }
    }

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK)
    {
        GSUSB_LOGE("GSUSB", "twai_driver_install failed: %s", esp_err_to_name(err));
        can_initialized = false;
        return false;
    }

    can_initialized = true;
    GSUSB_LOGI("GSUSB", "twai_driver_install OK");
    return true;
}

static void stop_can()
{
    if (!can_initialized)
    {
        GSUSB_LOGW("GSUSB", "stop_can called but CAN not initialized");
        return;
    }

    if (can_active)
    {
        esp_err_t err = twai_stop();
        if (err == ESP_OK)
        {
            GSUSB_LOGI("GSUSB", "CAN stopped (MODE RESET)");
        }
        else
        {
            GSUSB_LOGE("GSUSB", "twai_stop failed in stop_can: %s", esp_err_to_name(err));
        }
        can_active = false;
    }
    else
    {
        GSUSB_LOGI("GSUSB", "stop_can: CAN already inactive");
    }
}

extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport,
                                           uint8_t stage,
                                           tusb_control_request_t const *request)
{
    switch (stage)
    {
    case CONTROL_STAGE_SETUP:
        GSUSB_LOGI("GSUSB", "CTRL SETUP: bReq=%u bmReq=0x%02X wLength=%u",
                   request->bRequest,
                   request->bmRequestType,
                   request->wLength);

        switch (request->bRequest)
        {
      
        case GS_USB_BREQ_IDENTIFY:
            GSUSB_LOGI("GSUSB", "REQ IDENTIFY");
            return tud_control_xfer(rhport,
                                    request,
                                    (void *)&gs_resp_identify,
                                    sizeof(gs_resp_identify));

        case GS_USB_BREQ_DEVICE_CONFIG:
            GSUSB_LOGI("GSUSB", "REQ DEVICE_CONFIG");
            return tud_control_xfer(rhport,
                                    request,
                                    (void *)&gs_resp_conf,
                                    sizeof(gs_resp_conf));

        case GS_USB_BREQ_BT_CONST:
            GSUSB_LOGI("GSUSB", "REQ BT_CONST");
            return tud_control_xfer(rhport,
                                    request,
                                    (void *)&gs_resp_btc,
                                    sizeof(gs_resp_btc));

        
        case GS_USB_BREQ_HOST_FORMAT:
            GSUSB_LOGI("GSUSB", "REQ HOST_FORMAT (OUT)");
            return tud_control_xfer(rhport,
                                    request,
                                    (void *)&temp_host_format,
                                    sizeof(temp_host_format));

        case GS_USB_BREQ_BITTIMING:
            GSUSB_LOGI("GSUSB", "REQ BITTIMING (OUT)");
            return tud_control_xfer(rhport,
                                    request,
                                    (void *)&temp_bt,
                                    sizeof(temp_bt));

        case GS_USB_BREQ_MODE:
            GSUSB_LOGI("GSUSB", "REQ MODE (OUT)");
            return tud_control_xfer(rhport,
                                    request,
                                    (void *)&temp_mode,
                                    sizeof(temp_mode));

        default:
            GSUSB_LOGE("GSUSB", "Unsupported vendor request in SETUP: bReq=%u", request->bRequest);
            return false;
        }
        break;

    case CONTROL_STAGE_DATA:
        GSUSB_LOGI("GSUSB", "CTRL DATA stage: bReq=%u", request->bRequest);
        xSemaphoreTake(can_mutex, portMAX_DELAY);

        if (request->bRequest == GS_USB_BREQ_BITTIMING)
        {
            GSUSB_LOGI("GSUSB",
                       "CTRL DATA: BITTIMING (prop=%" PRIu32
                       " p1=%" PRIu32 " p2=%" PRIu32
                       " sjw=%" PRIu32 " brp=%" PRIu32 ")",
                       temp_bt.prop_seg,
                       temp_bt.phase_seg1,
                       temp_bt.phase_seg2,
                       temp_bt.sjw,
                       temp_bt.brp);

            if (!set_can_bittiming(&temp_bt))
            {
                GSUSB_LOGE("GSUSB", "set_can_bittiming failed in CTRL DATA");
            }
        }
        else if (request->bRequest == GS_USB_BREQ_MODE)
        {
            GSUSB_LOGI("GSUSB", "CTRL DATA: MODE mode=%" PRIu32 " flags=%" PRIu32,
                       temp_mode.mode, temp_mode.flags);

            if (temp_mode.mode == GS_CAN_MODE_START)
            {
                if (!can_initialized)
                {
                    GSUSB_LOGE("GSUSB", "MODE START but CAN not initialized (no BITTIMING yet)");
                }
                else if (!can_active)
                {
                    esp_err_t err = twai_start();
                    if (err == ESP_OK)
                    {
                        can_active = true;
                        GSUSB_LOGI("GSUSB", "twai_start OK, CAN active");
                    }
                    else
                    {
                        GSUSB_LOGE("GSUSB", "twai_start failed: %s", esp_err_to_name(err));
                    }
                }
                else
                {
                    GSUSB_LOGI("GSUSB", "MODE START received but CAN already active");
                }
            }
            else if (temp_mode.mode == GS_CAN_MODE_RESET)
            {
                GSUSB_LOGI("GSUSB", "MODE RESET received");
                stop_can();
            }
            else
            {
                GSUSB_LOGW("GSUSB", "Unknown MODE value=%" PRIu32, temp_mode.mode);
            }
        }

        xSemaphoreGive(can_mutex);
        return true;

    case CONTROL_STAGE_ACK:
        GSUSB_LOGI("GSUSB", "CTRL ACK stage bReq=%u", request->bRequest);
        return true;

    default:
        return true;
    }
}


extern "C" void tud_vendor_rx_cb(uint8_t itf, uint8_t const *buffer, uint16_t bufsize)
{
    (void)itf;
    (void)buffer;
    (void)bufsize;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (h_usb_tx_task != nullptr)
    {
        vTaskNotifyGiveFromISR(h_usb_tx_task, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}


extern "C" void can_rx_task(void *arg)
{
    (void)arg;

    struct gs_host_frame frame;
    twai_message_t msg;
    esp_err_t ret;

    GSUSB_LOGI("GSUSB", "can_rx_task started");

    for (;;)
    {
        if (!can_initialized || !can_active)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        ret = twai_receive(&msg, pdMS_TO_TICKS(1000));

        if (ret == ESP_OK)
        {
            if (tud_vendor_mounted())
            {
                memset(&frame, 0, sizeof(frame));
                // RX frame
                frame.echo_id  = 0xFFFFFFFF; 
                frame.channel  = 0;
                frame.flags    = 0;
                frame.reserved = 0;

                frame.can_id = msg.identifier;

                if (msg.extd)
                {
                    // CAN_EFF_FLAG
                    frame.can_id |= 0x80000000U; 
                }
                if (msg.rtr)
                {
                     // CAN_RTR_FLAG
                    frame.can_id |= 0x40000000U;
                }

                frame.can_dlc = msg.data_length_code;
                if (frame.can_dlc > 8)
                {
                    frame.can_dlc = 8;
                }

                if (frame.can_dlc > 0)
                {
                    memcpy(frame.data, msg.data, frame.can_dlc);
                }

                GSUSB_LOGI("GSUSB", "CAN RX: id=0x%08" PRIx32 " dlc=%u",
                           frame.can_id, frame.can_dlc);

                uint32_t avail = tud_vendor_write_available();
                if (avail >= sizeof(frame))
                {
                    uint32_t written = tud_vendor_write(&frame, sizeof(frame));
                    tud_vendor_write_flush();

                    if (written != sizeof(frame))
                    {
                        GSUSB_LOGE("GSUSB",
                                   "tud_vendor_write wrote %u/%u bytes",
                                   (unsigned)written,
                                   (unsigned)sizeof(frame));
                    }
                }
                else
                {
                    GSUSB_LOGE("GSUSB",
                               "USB TX buffer full, dropping frame (avail=%u)",
                               (unsigned)avail);
                }
            }
        }
        else if (ret == ESP_ERR_TIMEOUT)
        {
            continue;
        }
        else
        {
            GSUSB_LOGE("GSUSB", "twai_receive error: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

extern "C" void usb_tx_task(void *arg)
{
    (void)arg;

    struct gs_host_frame frame __attribute__((aligned(4)));
    twai_message_t msg;

    GSUSB_LOGI("GSUSB", "usb_tx_task started");

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (tud_vendor_available() >= sizeof(frame))
        {
            uint32_t count = tud_vendor_read(&frame, sizeof(frame));
            if (count != sizeof(frame))
            {
                GSUSB_LOGE("GSUSB",
                           "tud_vendor_read partial frame: %u/%u bytes",
                           (unsigned)count,
                           (unsigned)sizeof(frame));
                continue;
            }

            GSUSB_LOGI("GSUSB",
                       "USB RX: echo_id=%" PRIu32 " can_id=0x%08" PRIx32 " dlc=%u",
                       frame.echo_id, frame.can_id, frame.can_dlc);

            xSemaphoreTake(can_mutex, portMAX_DELAY);

            if (can_initialized && can_active)
            {
                memset(&msg, 0, sizeof(msg));
                msg.extd = (frame.can_id & 0x80000000U) ? 1 : 0;
                msg.rtr  = (frame.can_id & 0x40000000U) ? 1 : 0;
                msg.identifier = frame.can_id & 0x1FFFFFFF;
                msg.data_length_code = frame.can_dlc;
                if (msg.data_length_code > 8)
                    msg.data_length_code = 8;
                memcpy(msg.data, frame.data, msg.data_length_code);

                esp_err_t tx_err = twai_transmit(&msg, 0);
                if (tx_err != ESP_OK)
                {
                    GSUSB_LOGE("GSUSB", "twai_transmit failed: %s", esp_err_to_name(tx_err));
                }
                else
                {
                    if (tud_vendor_write_available() >= sizeof(frame))
                    {
                        uint32_t w = tud_vendor_write(&frame, sizeof(frame));
                        tud_vendor_write_flush();
                        GSUSB_LOGI("GSUSB",
                                   "Echo TX frame back to host, written=%u",
                                   (unsigned)w);
                    }
                }
            }
            else
            {
                GSUSB_LOGW("GSUSB", "USB RX frame but CAN is not active/initialized");
            }

            xSemaphoreGive(can_mutex);
        }
    }
}

extern "C" void tinyusb_task(void *param)
{
    (void)param;
    GSUSB_LOGI("GSUSB", "tinyusb_task started");

    for (;;)
    {
        tud_task();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}



static const uint8_t vendor_conf_desc[] = {
    
    9, TUSB_DESC_CONFIGURATION,
    U16_TO_U8S_LE(TUSB_DESC_TOTAL_LEN),
    1,    // Number of interfaces
    1,    // Configuration value
    0,    // Configuration string index
    0x80, // Attributes (bus powered)
    50,   // Max power (100mA)

    // Interface descriptor
    9, TUSB_DESC_INTERFACE,
    0, // Interface number
    0, // Alternate setting
    2, // Number of endpoints
    TUSB_CLASS_VENDOR_SPECIFIC,
    0,
    0,
    0, // String index

    // Endpoint OUT 
    7, TUSB_DESC_ENDPOINT,
    0x01, // EP 1 OUT
    TUSB_XFER_BULK,
    U16_TO_U8S_LE(64),
    0,

    // Endpoint IN 
    7, TUSB_DESC_ENDPOINT,
    0x81, // EP 1 IN
    TUSB_XFER_BULK,
    U16_TO_U8S_LE(64),
    0
};


void gsusb_init(void)
{
    can_mutex = xSemaphoreCreateMutex();
    if (can_mutex == nullptr)
    {
        printf("Failed to create CAN mutex.\n");
        return;
    }

    tinyusb_config_t tusb_cfg = {};
    tusb_cfg.external_phy = false;
    tusb_cfg.configuration_descriptor = vendor_conf_desc;

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK)
    {
        printf("tinyusb_driver_install failed: %d\n", (int)err);
        return;
    }

    xTaskCreate(tinyusb_task, "tinyusb", 4096, nullptr, 10, nullptr);
    xTaskCreate(can_rx_task, "can_rx", 4096, nullptr, 9, nullptr);
    xTaskCreate(usb_tx_task, "usb_tx", 4096, nullptr, 8, &h_usb_tx_task);

    printf("CandleLight Firmware Running (GS-USB, modularized).\n");

    LedService::getInstance().start();
    LedService::getInstance().setStatusLed(3);
}
