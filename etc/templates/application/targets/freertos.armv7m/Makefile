OPENMRNPATH ?= $(shell \
sh -c "if [ \"X`printenv OPENMRNPATH`\" != \"X\" ]; then printenv OPENMRNPATH; \
     elif [ -d ~/openmrn/src ]; then echo ~/openmrn; \
     elif [ -d /opt/openmrn/src ]; then echo /opt/openmrn; \
     else echo OPENMRNPATH not found; fi" \
)

# Find StellarisWare
include $(OPENMRNPATH)/etc/stellarisware.mk

LDFLAGSEXTRA = -L$(STELLARISWAREPATH)/driverlib/gcc-cm3 \
               -L$(STELLARISWAREPATH)/usblib/gcc-cm3
SYSLIBRARIESEXTRA = -lusb-cm3 -ldriver-cm3
OBJEXTRA = $(OPENMRNPATH)/targets/freertos.armv7m/freertos_drivers/stellaris_can.o \
           $(OPENMRNPATH)/targets/freertos.armv7m/freertos_drivers/stellaris_uart.o \
           $(OPENMRNPATH)/targets/freertos.armv7m/freertos_drivers/stellaris_usb_cdc_device.o

##TARGET##

include $(OPENMRNPATH)/etc/prog.mk
