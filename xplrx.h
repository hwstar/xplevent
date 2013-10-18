#ifndef XPLRX_H
#define XPLRX_H

#define XHCM_TERM_REQUEST 0x55

void XplRXDestroy(void *objPtr);
void *XplRXInit(int localConnFD, int localConnPort, int rxReadyFD);
Bool XplrxSendControlMsg(void *xplrxheader, int val);
String XplrxDQRawString(TALLOC_CTX *ctx, void *xplrxheader);
int XplrxGetAndResetWdogCounter(void *objPtr);

#endif
