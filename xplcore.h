#ifndef XPLCORE_H
#define XPLCORE_H

void XplDestroy(void *objPtr);
void *XplInit(TALLOC_CTX *ctx, void *Poller, String InterfaceIP);


#endif
