#ifndef XPLRX_H
#define XPLRX_H


void *XplRXInit(TALLOC_CTX *ctx, int rxReadyFD);
Bool XplrxSendControlMsg(void *xplrxheader, int val);
String XplrxDQRawString(TALLOC_CTX *ctx, void *xplrxheader);

#endif
