# In this file you can specify the upload method configuration for your custom target(s).
# See here for the list of parameters that can be set:
# https://github.com/mbed-ce/mbed-os/wiki/Upload-Methods

if( MBED_TARGET STREQUAL "TINY2040" OR
    MBED_TARGET STREQUAL "QT_PY_RP2040" OR
    MBED_TARGET STREQUAL "RASPBERRY_PI_PICO")

	set(PICOTOOL_UPLOAD_ENABLED TRUE)
	set(MBED_UPLOAD_ENABLED TRUE)
	set(UPLOAD_METHOD_DEFAULT PICOTOOL)

endif()
