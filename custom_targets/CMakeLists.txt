
add_library(mbed-tiny2040 INTERFACE)
target_link_libraries(mbed-tiny2040 INTERFACE mbed-raspberry-pi-pico)

add_library(mbed-qt-py-rp2040 INTERFACE)
target_link_libraries(mbed-qt-py-rp2040 INTERFACE mbed-raspberry-pi-pico)
