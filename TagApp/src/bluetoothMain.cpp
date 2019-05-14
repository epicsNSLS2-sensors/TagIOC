#include <stddef.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <iostream>

#include "epicsExit.h"
#include "epicsThread.h"
#include "iocsh.h"

#include "dbDefs.h"
#include "registryFunction.h"
#include "epicsExport.h"

#include "tag.h"

int main(int argc,char *argv[])
{
    if(argc>=2) {    
        iocsh(argv[1]);
        epicsThreadSleep(.2);
    }
    iocsh(NULL);
    epicsExit(0);
    return(0);
}

void tagConfig(char *mac) {
	strncpy(mac_address, mac, strlen(mac));
}

static const iocshArg tagConfigArg0 = {"MAC address", iocshArgString};
static const iocshArg * const tagConfigArgs[] = {&tagConfigArg0};
static const iocshFuncDef configtag = {"tagConfig", 1, tagConfigArgs};
static void configtagCallFunc(const iocshArgBuf *args) {
	tagConfig(args[0].sval);
}

static void tagRegister(void) {
	iocshRegister(&configtag, configtagCallFunc);
}

extern "C" {
	epicsExportRegistrar(tagRegister);
}