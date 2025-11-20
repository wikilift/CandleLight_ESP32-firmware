idf_component_register(SRCS "main.cpp"
                            "gsusb_device/gsusb_can.cpp"
                            "gsusb_device/gsusb_usb.cpp"
                       INCLUDE_DIRS .
                       "constants"
                       "services"
                       "definitions"
                       "debug"
                       "gsusb_device"

                       )
