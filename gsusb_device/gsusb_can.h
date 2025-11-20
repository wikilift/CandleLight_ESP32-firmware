#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/twai.h"
#include "gs_usb.h"

#ifdef __cplusplus
extern "C" {
#endif

void gsusb_can_init(void);


bool gsusb_can_set_bittiming(const struct gs_device_bittiming *bt);

esp_err_t gsusb_can_start(void);
void      gsusb_can_stop(void);


bool gsusb_can_is_initialized(void);
bool gsusb_can_is_active(void);

SemaphoreHandle_t gsusb_can_get_mutex(void);

esp_err_t gsusb_can_receive(twai_message_t *msg, TickType_t timeout);
esp_err_t gsusb_can_transmit(const twai_message_t *msg, TickType_t timeout);

#ifdef __cplusplus
}
#endif
