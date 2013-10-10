#ifndef XPLCORE_H
#define XPLCORE_H

/* Possible xPL message types */
typedef enum { XPL_MESSAGE_ANY, XPL_MESSAGE_COMMAND, XPL_MESSAGE_STATUS, XPL_MESSAGE_TRIGGER } XPLMessageType_t;

/* Signature of a service message listener function */
typedef void (* XPLListenerFunc_t )(void *XPLMessage, void *userObj);

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
void XplClearNameValues(void *XPLMessage);
void XplAddNameValue(void *XPLMessage, String theName, String theValue);
Bool XplSendMessage(void *XPLMessage);
void XplAddMessageListener(void *XPLService, Bool reportAllMessages,
Bool reportOwnMessages, void *userObj, void (*listener)(void *XPLMessage, void *userObj));
void XplRemoveMessageListener(void *XPLService);
void XplGetMessageSourceTagComponents(void *XPLMessage, TALLOC_CTX *stringCTX,
	String *theVendor, String *theDeviceID, String *theInstanceID);
XPLMessageType_t XplGetMessageType(void *XPLMessage);
void XplGetMessageSchema(void *XPLMessage, TALLOC_CTX *stringCTX, String *theClass,  String *theType);
Bool XplMessageIsBroadcast(void *XPLMessage);

#endif
