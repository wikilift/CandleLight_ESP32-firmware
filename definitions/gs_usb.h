#pragma once


#define GS_USB_BREQ_HOST_FORMAT 0
#define GS_USB_BREQ_BITTIMING 1
#define GS_USB_BREQ_MODE 2
#define GS_USB_BREQ_BERR 3
#define GS_USB_BREQ_BT_CONST 4
#define GS_USB_BREQ_DEVICE_CONFIG 5
#define GS_USB_BREQ_TIMESTAMP 6
#define GS_USB_BREQ_IDENTIFY 7

#define GS_CAN_MODE_RESET 0
#define GS_CAN_MODE_START 1

struct __attribute__((packed)) gs_device_config
{
    uint8_t reserved1;
    uint8_t reserved2;
    uint8_t reserved3;
    uint8_t icount;
    uint32_t sw_version;
    uint32_t hw_version;
};

struct __attribute__((packed)) gs_device_bt_const
{
    uint32_t feature;
    uint32_t fclk_can;
    uint32_t tseg1_min;
    uint32_t tseg1_max;
    uint32_t tseg2_min;
    uint32_t tseg2_max;
    uint32_t sjw_max;
    uint32_t brp_min;
    uint32_t brp_max;
    uint32_t brp_inc;
};

struct __attribute__((packed)) gs_device_bittiming
{
    uint32_t prop_seg;
    uint32_t phase_seg1;
    uint32_t phase_seg2;
    uint32_t sjw;
    uint32_t brp;
};

struct __attribute__((packed)) gs_device_mode
{
    uint32_t mode;
    uint32_t flags;
};

struct __attribute__((packed)) gs_host_frame
{
    uint32_t echo_id;
    uint32_t can_id;
    uint8_t can_dlc;
    uint8_t channel;
    uint8_t flags;
    uint8_t reserved;
    uint8_t data[8];
};

struct __attribute__((packed)) gs_host_config
{
    uint32_t byte_order;  
};
