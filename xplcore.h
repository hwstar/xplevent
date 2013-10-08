#ifndef XPLCORE_H
#define XPLCORE_H

/* Possible xPL message types */
typedef enum { XPL_MESSAGE_ANY, XPL_MESSAGE_COMMAND, XPL_MESSAGE_STATUS, XPL_MESSAGE_TRIGGER } XPLMessageType_t;


/* Master object creation and destruction */

void XplDestroy(void *objPtr);
void *XplInit(TALLOC_CTX *ctx, void *Poller, String RemoteIP, String BroadcastIP, String InternalIP);

/* Service support */

void *XplNewService(void *xplObj, String theVendor, String theDeviceID, String theInstanceID);
Bool XplDestroyService(void *xplObj, void *servToDestroy);
void XplEnableService(void *xplObj, void *servToEnable);
void XplDisableService(void *xplObj, void *servToDisable);

/* Message Support */

void *XplInitMessage(void *XPLServ, XPLMessageType_t messageType, 
String theVendor, String theDeviceID, String theInstanceID);
void XplDestroyMessage(void *XPLMessage);
void XplDestroyNameValues(void *XPLMessage);
void XplAddNameValue(void *XPLMessage, String theName, String theValue);
void XplSendMessage(void *XPLMessage);

#endif
