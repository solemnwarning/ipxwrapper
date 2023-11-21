#ifndef IPXWRAPPER_SENDER_H
#define IPXWRAPPER_SENDER_H

#include <stdbool.h>
#include <winsock2.h>

struct SenderQueue;
typedef struct SenderQueue SenderQueue;

struct SenderThread;
typedef struct SenderThread SenderThread;

SenderQueue *SenderQueue_new(void);
void SenderQueue_destroy(SenderQueue *self);

bool SenderQueue_send(SenderQueue *queue, int sock,
	const void *header_data, unsigned int header_size,
	const void *data, unsigned int data_size,
	const struct sockaddr *addr, int addrlen);

SenderThread *SenderThread_new(SenderQueue *queue);
void SenderThread_destroy(SenderThread *self);

#endif /* !IPXWRAPPER_SENDER_H */
