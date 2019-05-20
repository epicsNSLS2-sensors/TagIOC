TOP=../..

#CFLAGS += -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include

include $(TOP)/configure/CONFIG
#=============================
# Build the IOC application

PROD_IOC = tag
# tag.dbd will be created and installed
DBD += tag.dbd

# tag.dbd will be made up from these files:
tag_DBD += base.dbd
#tag_DBD += asyn.dbd
#tag_DBD += drvAsynIPPort.dbd
tag_DBD += bluetooth.dbd

# tag_registerRecordDeviceDriver.cpp derives from tag.dbd
tag_SRCS += tag_registerRecordDeviceDriver.cpp

tag_SRCS += bluetooth.c

# Build the main IOC entry point on workstation OSs.
tag_SRCS_DEFAULT += bluetoothMain.cpp

# Link in the code from the support library
#tag_LIBS += asyn

# Finally link to the EPICS Base libraries
tag_LIBS += $(EPICS_BASE_IOC_LIBS)

# Link bluetooth libraries
tag_SYS_LIBS += gattlib
tag_SYS_LIBS += glib-2.0
tag_SYS_LIBS += pthread
#tag_SYS_LIBS += $(shell pkg-config --libs glib-2.0)

CFLAGS += $(shell pkg-config --cflags glib-2.0)

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

