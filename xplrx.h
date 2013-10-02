#ifndef XPLRX_H
#define XPLRX_H


void *XplRXInit(TALLOC_CTX *ctx);
Bool XplrxSendEvent(void *xplrxheader, int val);

#endif
