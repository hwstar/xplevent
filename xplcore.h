#ifndef XPLCORE_H
#define XPLCORE_H

/* Possible xPL message types */
typedef enum { XPL_MESSAGE_ANY, XPL_MESSAGE_COMMAND, XPL_MESSAGE_STATUS, XPL_MESSAGE_TRIGGER } XPLMessageType_t;
typedef enum { XPL_REPORT_MODE_NORMAL, XPL_REPORT_OWN_MESSAGES, XPL_REPORT_EVERYTHING } XPLListenerReportMode_t;

/* Signature of a service message listener function */
typedef void (* XPLListenerFunc_t )(void *XPLMessage, void *XPLService, void *userObj);
typedef void (* XPLIterateNVCallback_t)(void *userObj, const String name, const String value);
/* Master object creation and destruction */

void XplDestroy(void *objPtr);
void *XplInit(TALLOC_CTX *ctx, void *Poller, String IPAddr, unsigned port);


/* Service support */

void *XplNewService(void *xplObj, String theVendor, String theDeviceID, String theInstanceID);
Bool XplDestroyService(void *servToDestroy);
void XplEnableService(void *servToEnable);
void XplDisableService(void *servToDisable);

/* Message Support */

void *XplInitMessage(void *XPLServ, XPLMessageType_t messageType, 
String theVendor, String theDeviceID, String theInstanceID);
void XplDestroyMessage(void *XPLMessage);
void XplSetMessageClassType(void *xplMessage, const String class, const String type);
void XplClearNameValues(void *XPLMessage);
void XplAddNameValue(void *XPLMessage, String theName, String theValue);
Bool XplSendMessage(void *XPLMessage);
void XplAddMessageListener(void *XPLService, XPLListenerReportMode_t reportMode, void *userObj, XPLListenerFunc_t listener);
void XplRemoveMessageListener(void *XPLService);
void XplGetMessageSourceTagComponents(void *XPLMessage, TALLOC_CTX *stringCTX,
	String *theVendor, String *theDeviceID, String *theInstanceID);
XPLMessageType_t XplGetMessageType(void *XPLMessage);
void XplGetMessageSchema(void *XPLMessage, TALLOC_CTX *stringCTX, String *theClass,  String *theType);
Bool XplMessageIsBroadcast(void *XPLMessage);
Bool XplMessageIsReceive(void *XPLMessage);
String XplGetMessageNameValuesAsString(TALLOC_CTX *stringCTX, void *XPLMessage);
void XplMessageIterateNameValues(void *XPLMessage, void *userObj, XPLIterateNVCallback_t callback );
String XplGetMessageValueByName(void *XPLMessage, TALLOC_CTX *stringCTX, String theName);

#endif
