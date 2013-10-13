#ifndef XPLCORE_H
#define XPLCORE_H

/* Possible XPL message types */
typedef enum { XPL_MESSAGE_ANY = 0, XPL_MESSAGE_COMMAND, XPL_MESSAGE_STATUS, XPL_MESSAGE_TRIGGER } XPLMessageType_t;
typedef enum { XPL_REPORT_MODE_NORMAL = 0, XPL_REPORT_OWN_MESSAGES, XPL_REPORT_EVERYTHING } XPLListenerReportMode_t;
typedef enum { XPL_HUB_UNCONFIRMED = 0, XPL_HUB_NO_ECHO, XPL_HUB_CONFIRMED } XPLDiscoveryState_t;
typedef enum { XPL_MSG_ID_NORMAL = 0, XPL_MSG_ID_GROUP, XPL_MSG_ID_HEARTBEAT } XPLMessageID_t;

/* Signature of a service message listener function */
typedef void (* XPLListenerFunc_t )(void *XPLMessage, void *XPLService, void *userObj, XPLMessageID_t msgID, Bool broadcast);
/* Signature of a name-value callback function */
typedef void (* XPLIterateNVCallback_t)(void *userObj, const String name, const String value);

/* Master object creation and destruction */

void XplDestroy(void *objPtr);
void *XplInit(TALLOC_CTX *ctx, void *Poller, String IPAddr, String servicePort);


/* Service support */

void *XplNewService(void *xplObj, String theVendor, String theDeviceID, String theInstanceID, String theVersion);
Bool XplDestroyService(void *servToDestroy);
void XplEnableService(void *servToEnable);
void XplDisableService(void *servToDisable);
XPLDiscoveryState_t XplGetHubDiscoveryState(void  *servToCheck);

/* Message Support */

void *XplInitTargettedMessage(void *XPLServ, XPLMessageType_t messageType, 
String theVendor, String theDeviceID, String theInstanceID);
void *XplInitBroadcastMessage(void *XPLServ, XPLMessageType_t messageType);
void *XplInitGroupMessage(void *XPLServ, XPLMessageType_t messageType, String controlGroup);
void XplDestroyMessage(void *XPLMessage);
void XplSetMessageClassType(void *xplMessage, const String class, const String type);
void XplClearNameValues(void *XPLMessage);
void XplAddNameValue(void *XPLMessage, String theName, String theValue);
Bool XplSendMessage(void *XPLMessage);
void XplAddMessageListener(void *XPLService, XPLListenerReportMode_t reportMode, Bool reportGroupMessages,
	void *userObj, XPLListenerFunc_t listener);
void XplRemoveMessageListener(void *XPLService);
void XplGetMessageSourceTagComponents(void *XPLMessage, TALLOC_CTX *stringCTX,
	String *theVendor, String *theDeviceID, String *theInstanceID);
XPLMessageType_t XplGetMessageType(void *XPLMessage);
void XplGetMessageSchema(void *XPLMessage, TALLOC_CTX *stringCTX, String *theClass,  String *theType);
Bool XplMessageIsReceive(void *XPLMessage);
String XplGetMessageNameValuesAsString(TALLOC_CTX *stringCTX, void *XPLMessage);
void XplMessageIterateNameValues(void *XPLMessage, void *userObj, XPLIterateNVCallback_t callback );
String XplGetMessageValueByName(void *XPLMessage, TALLOC_CTX *stringCTX, String theName);

#endif
