#include "gsusb_usb.h"
#include "led_service.h"

extern "C" void app_main()
{
    LedService::getInstance().start();

   
    if (gsusb_init() != ESP_OK)
    {
        LedService::getInstance().setStatusLed(LED_ERROR);
    }
}
