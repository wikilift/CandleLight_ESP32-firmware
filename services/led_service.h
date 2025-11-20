#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"
#include "dbg_helpers.h"
#include "board_pins.h"

enum LedEvents
{
    LED_EVENT_SET_STATE = (1U << 0), 
    LED_EVENT_FLASH     = (1U << 1),
    LED_EVENT_STARTUP   = (1U << 2)  
};

enum LedStatus
{
    LED_OFF = 0,
    LED_ACTIVE = 1,     
    LED_ERROR = 5,    
    LED_RX_FLASH = 6,   
    LED_TX_FLASH = 7
};

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
        configureLed();
        xTaskCreate(&ledTaskTrampoline, "ledAnimator", 2048, this, tskIDLE_PRIORITY + 1, &h_led_task);
       
        vTaskDelay(pdMS_TO_TICKS(50)); 
        if (h_led_task != nullptr) {
            xTaskNotify(h_led_task, LED_EVENT_STARTUP, eSetBits);
        }
    }

    void setStatusLed(int status)
    {
        if (h_led_task != nullptr) {
            statusLed = status;
            xTaskNotify(h_led_task, LED_EVENT_SET_STATE, eSetBits);
        }
    }

    void flashRxLed()
    {
        if (statusLed != LED_ACTIVE) return;
        if (h_led_task != nullptr) {
            flashStatus = LED_RX_FLASH;
            xTaskNotify(h_led_task, LED_EVENT_FLASH, eSetBits);
        }
    }
    
    void flashTxLed()
    {
        if (statusLed != LED_ACTIVE) return;
        if (h_led_task != nullptr) {
            flashStatus = LED_TX_FLASH;
            xTaskNotify(h_led_task, LED_EVENT_FLASH, eSetBits);
        }
    }

private:
    led_strip_handle_t ledStrip;
    volatile int statusLed = LED_ACTIVE; 
    volatile int flashStatus = LED_RX_FLASH; 
    TaskHandle_t h_led_task = nullptr;
    const int numLeds = 1;
    const gpio_num_t dataPin = RGB_PIN;

    static const constexpr char *TAG = "LED";
    const float BRIGHTNESS = 0.1;

    LedService() {}
    LedService(const LedService &) = delete;
    LedService &operator=(const LedService &) = delete;

    static void ledTaskTrampoline(void *pvParameter)
    {
        static_cast<LedService *>(pvParameter)->ledTask();
    }

    void setLedColorSync(uint8_t r, uint8_t g, uint8_t b)
    {
        updateLedColor(r, g, b, BRIGHTNESS);
    }
    
    void clearLedSync()
    {
        led_strip_set_pixel(ledStrip, 0, 0, 0, 0);
        led_strip_refresh(ledStrip);
    }
    
    void updateLedColor(uint8_t r, uint8_t g, uint8_t b, float brightness)
    {
        uint8_t R = (uint8_t)(r * brightness);
        uint8_t G = (uint8_t)(g * brightness);
        uint8_t B = (uint8_t)(b * brightness);

        led_strip_set_pixel(ledStrip, 0, R, G, B);
        led_strip_refresh(ledStrip);
    }

    void configureLed()
    {
        led_strip_config_t stripConfig = {};
        stripConfig.strip_gpio_num = dataPin;
        stripConfig.max_leds = numLeds;

        led_strip_rmt_config_t rmtConfig = {};
        rmtConfig.resolution_hz = 10 * 1000 * 1000;
        rmtConfig.flags.with_dma = false;

        ESP_ERROR_CHECK(led_strip_new_rmt_device(&stripConfig, &rmtConfig, &ledStrip));
        led_strip_clear(ledStrip);
    }

    void runStartupSequence()
    {
       
        for (int i = 0; i < 3; ++i) 
        {
            setLedColorSync(0, 255, 0); 
            vTaskDelay(pdMS_TO_TICKS(100));
            clearLedSync();             
            vTaskDelay(pdMS_TO_TICKS(100));
        }
       
        statusLed = LED_ACTIVE;
        setLedColorSync(0, 255, 0);
    }
    
    void getLedParams(int state, uint8_t &r, uint8_t &g, uint8_t &b, uint32_t &delay)
    {
        switch (state) {
            case LED_ACTIVE:
                r = 0; g = 255; b = 0; delay = portMAX_DELAY; 
                break;
            case LED_ERROR:
                r = 255; g = 0; b = 0; delay = 200; 
                break;
            case LED_RX_FLASH:
                r = 0; g = 255; b = 0; delay = 50; 
                break;
            case LED_TX_FLASH:
                r = 255; g = 165; b = 0; delay = 50; 
                break;
            case LED_OFF:
            default:
                r = 0; g = 0; b = 0; delay = portMAX_DELAY;
                break;
        }
    }

    void ledTask()
    {
        uint32_t notifiedValue;
        uint32_t currentDelay_ms = portMAX_DELAY;
        bool startupDone = false;
        
        uint8_t dummy_r, dummy_g, dummy_b; 
        uint32_t dummy_delay;

        for (;;)
        {
            BaseType_t notified = xTaskNotifyWait(0x00, 
                                                  0xFFFFFFFF,
                                                  &notifiedValue,
                                                  startupDone ? currentDelay_ms : portMAX_DELAY);
            
           
            if (notified == pdTRUE && (notifiedValue & LED_EVENT_STARTUP) && !startupDone) {
                runStartupSequence();
                getLedParams(statusLed, dummy_r, dummy_g, dummy_b, currentDelay_ms);
                startupDone = true;
                continue;
            }

            if (!startupDone) {
                continue;
            }
            
            if (notified == pdTRUE && (notifiedValue & LED_EVENT_FLASH)) {
                uint8_t r, g, b;
                uint32_t flash_delay;
                
             
                uint8_t prev_r, prev_g, prev_b;
                getLedParams(statusLed, prev_r, prev_g, prev_b, dummy_delay);
                
              
                getLedParams(flashStatus, r, g, b, flash_delay);
                setLedColorSync(r, g, b);
                vTaskDelay(pdMS_TO_TICKS(flash_delay));

              
                setLedColorSync(prev_r, prev_g, prev_b);
                continue;
            }

          
            if (notified == pdTRUE && (notifiedValue & LED_EVENT_SET_STATE)) {
                getLedParams(statusLed, dummy_r, dummy_g, dummy_b, currentDelay_ms);
                
              
                if (statusLed == LED_ACTIVE) {
                    setLedColorSync(0, 255, 0); 
                } else if (statusLed == LED_OFF) {
                    clearLedSync();
                }
              
            }
            
        
            if (statusLed == LED_ERROR) {
                static bool isOn = false;
                isOn = !isOn; 

                if (isOn) {
                    setLedColorSync(255, 0, 0); 
                } else {
                    clearLedSync();
                }
            }
        }
    }
};