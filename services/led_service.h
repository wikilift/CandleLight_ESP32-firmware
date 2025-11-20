#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"
#include "dbg_helpers.h"
#ifdef CONFIG_IDF_TARGET_ESP32S3
    #define RGB_PIN GPIO_NUM_38
#else
    #define RGB_PIN GPIO_NUM_5
#endif
class LedService
{
public: 
    static LedService &getInstance()
    {
        static LedService instance;
        return instance;
    }

    void start()
    {
        GSUSB_LOGI(TAG, "Starting LED...");
        configureLed();
        xTaskCreate(&ledTaskTrampoline, "ledBlink", 2048, this, tskIDLE_PRIORITY, nullptr);
    }

    void setStatusLed(int status)
    {
        statusLed = status;
    }

private:
    led_strip_handle_t ledStrip;
    volatile int statusLed = 1;
    const int numLeds = 1;
    const gpio_num_t dataPin = RGB_PIN;

    static const constexpr char *TAG = "LED";

    LedService() {}

    LedService(const LedService &) = delete;
    LedService &operator=(const LedService &) = delete;

    static void ledTaskTrampoline(void *pvParameter)
    {
        static_cast<LedService *>(pvParameter)->ledTask();
    }

    void ledTask()
    {
        bool isOn = true;
        int delay = 200;
        while (true)
        {
            isOn = !isOn;
            switch (statusLed)
            {
            case 1:
                delay = 300;
                break;
            case 2:
                delay = 300;
                break;
            case 3:
                delay = 400;
                break;
            case 4:
                delay = 100;
                break;
            case 5:
                delay = 150;
                break;
            }
            updateLedColor(isOn);
            vTaskDelay(delay / portTICK_PERIOD_MS);
        }
    }

    void configureLed()
    {
        led_strip_config_t stripConfig = {};
        stripConfig.strip_gpio_num = dataPin;
        stripConfig.max_leds = numLeds;

        led_strip_rmt_config_t rmtConfig = {};
        rmtConfig.resolution_hz = 10 * 1000 * 1000;

        ESP_ERROR_CHECK(led_strip_new_rmt_device(&stripConfig, &rmtConfig, &ledStrip));
        led_strip_clear(ledStrip);
    }

    void updateLedColor(bool isOn)
    {
        float brightness = 0.1;

        if (!isOn)
        {
            led_strip_set_pixel(ledStrip, 0, 0, 0, 0);
        }
        else
        {
            uint8_t r = 0, g = 0, b = 0;

            switch (statusLed)
            {
            case 1:
                r = 0 * brightness;
                g = 255 * brightness;
                b = 0 * brightness; // Verde
                break;
            case 2:
                r = 255 * brightness;
                g = 127 * brightness;
                b = 80 * brightness; // Coral
                break;
            case 3:
                r = 138 * brightness;
                g = 43 * brightness;
                b = 226 * brightness; // BlueViolet
                break;
            case 4:
                r = 218 * brightness;
                g = 112 * brightness;
                b = 214 * brightness; // Orchid
                break;
            case 5:
                r = 255 * brightness;
                g = 0 * brightness;
                b = 0 * brightness; // Rojo
                break;
            }
            led_strip_set_pixel(ledStrip, 0, r, g, b);
        }
        led_strip_refresh(ledStrip);
    }
};