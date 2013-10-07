#ifndef XPLCORE_H
#define XPLCORE_H

/* Possible xPL message types */
typedef enum { XPL_MESSAGE_ANY, XPL_MESSAGE_COMMAND, XPL_MESSAGE_STATUS, XPL_MESSAGE_TRIGGER } XPLMessageType_t;


void XplDestroy(void *objPtr);
void *XplInit(TALLOC_CTX *ctx, void *Poller, String InterfaceIP);
void *XplNewService(void *pxi, String theVendor, String theDeviceID, String theInstanceID);
Bool XplDestroyService(void *xplObj, void *servToDestroy);
void XplEnableService(void *xplObj, void *servToEnable);
void XplDisableService(void *xplObj, void *servToDisable);


#endif
