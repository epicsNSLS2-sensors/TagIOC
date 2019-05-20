#!../../bin/linux-x86_64/tag

< envPaths


## Register all support components
dbLoadDatabase "$(TOP)/dbd/tag.dbd"
tag_registerRecordDeviceDriver pdbbase

tagConfig("A4:34:F1:F3:78:2B")

## Load record instances
dbLoadRecords "$(TOP)/db/sensors.db"

iocInit
