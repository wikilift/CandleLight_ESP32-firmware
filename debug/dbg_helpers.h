#define GSUSB_DEBUG 1

#if GSUSB_DEBUG
    #define GSUSB_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
    #define GSUSB_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
    #define GSUSB_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#else
    #define GSUSB_LOGI(tag, fmt, ...) do {} while (0)
    #define GSUSB_LOGE(tag, fmt, ...) do {} while (0)
    #define GSUSB_LOGW(tag, fmt, ...) do {} while (0)
#endif
